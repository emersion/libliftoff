#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "private.h"

/* Plane allocation algorithm
 *
 * Goal: KMS exposes a set of hardware planes, user submitted a set of layers.
 * We want to map as many layers as possible to planes.
 *
 * However, all layers can't be mapped to any plane. There are constraints,
 * sometimes depending on driver-specific limitations or the configuration of
 * other planes.
 *
 * The only way to discover driver-specific limitations is via an atomic test
 * commit: we submit a plane configuration, and KMS replies whether it's
 * supported or not. Thus we need to incrementally build a valid configuration.
 *
 * Let's take an example with 2 planes and 3 layers. Plane 1 is only compatible
 * with layer 2 and plane 2 is only compatible with layer 3. Our algorithm will
 * discover the solution by building the mapping one plane at a time. It first
 * starts with plane 1: an atomic commit assigning layer 1 to plane 1 is
 * submitted. It fails, because this isn't supported by the driver. Then layer
 * 2 is assigned to plane 1 and the atomic test succeeds. We can go on and
 * repeat the operation with plane 2. After exploring the whole tree, we end up
 * with a valid allocation.
 *
 *
 *                    layer 1                 layer 1
 *                  +---------> failure     +---------> failure
 *                  |                       |
 *                  |                       |
 *                  |                       |
 *     +---------+  |          +---------+  |
 *     |         |  | layer 2  |         |  | layer 3   final allocation:
 *     | plane 1 +------------>+ plane 2 +--+---------> plane 1 → layer 2
 *     |         |  |          |         |              plane 2 → layer 3
 *     +---------+  |          +---------+
 *                  |
 *                  |
 *                  | layer 3
 *                  +---------> failure
 *
 *
 * Note how layer 2 isn't considered for plane 2: it's already mapped to plane
 * 1. Also note that branches are pruned as soon as an atomic test fails.
 *
 * In practice, the primary plane is treated separately. This is where layers
 * that can't be mapped to any plane (e.g. layer 1 in our example) will be
 * composited. The primary plane is the first that will be allocated, because
 * some drivers require it to be enabled in order to light up any other plane.
 * Then all other planes will be allocated, from the topmost one to the
 * bottommost one.
 *
 * The "zpos" property (which defines ordering between layers/planes) is handled
 * as a special case. If it's set on layers, it adds additional constraints on
 * their relative ordering. If two layers intersect, their relative zpos needs
 * to be preserved during plane allocation.
 *
 * Implementation-wise, the output_choose_layers function is called at each node
 * of the tree. It iterates over layers, check constraints, performs an atomic
 * test commit and calls itself recursively on the next plane.
 */

/* Global data for the allocation algorithm */
struct alloc_result {
	drmModeAtomicReq *req;
	uint32_t flags;
	size_t planes_len;

	struct liftoff_layer **best;
	int best_score;

	/* per-output */
	bool has_composition_layer;
	size_t non_composition_layers_len;
};

/* Transient data, arguments for each step */
struct alloc_step {
	struct liftoff_list *plane_link; /* liftoff_plane.link */
	size_t plane_idx;

	struct liftoff_layer **alloc; /* only items up to plane_idx are valid */
	int score; /* number of allocated layers */
	int last_layer_zpos;

	bool composited; /* per-output */

	char log_prefix[64];
};

static void plane_step_init_next(struct alloc_step *step,
				 struct alloc_step *prev,
				 struct liftoff_layer *layer)
{
	struct liftoff_plane *plane;
	struct liftoff_layer_property *zpos_prop;
	size_t len;

	plane = liftoff_container_of(prev->plane_link, plane, link);

	step->plane_link = prev->plane_link->next;
	step->plane_idx = prev->plane_idx + 1;
	step->alloc = prev->alloc;
	step->alloc[prev->plane_idx] = layer;

	if (layer != NULL && layer == layer->output->composition_layer) {
		assert(!prev->composited);
		step->composited = true;
	} else {
		step->composited = prev->composited;
	}

	if (layer != NULL && layer != layer->output->composition_layer) {
		step->score = prev->score + 1;
	} else {
		step->score = prev->score;
	}

	zpos_prop = NULL;
	if (layer != NULL) {
		zpos_prop = layer_get_property(layer, "zpos");
	}
	if (zpos_prop != NULL && plane->type != DRM_PLANE_TYPE_PRIMARY) {
		step->last_layer_zpos = zpos_prop->value;
	} else {
		step->last_layer_zpos = prev->last_layer_zpos;
	}

	if (layer != NULL) {
		len = strlen(prev->log_prefix) + 2;
		if (len > sizeof(step->log_prefix) - 1) {
			len = sizeof(step->log_prefix) - 1;
		}
		memset(step->log_prefix, ' ', len);
		step->log_prefix[len] = '\0';
	} else {
		memcpy(step->log_prefix, prev->log_prefix,
		       sizeof(step->log_prefix));
	}
}

static bool is_layer_allocated(struct alloc_step *step,
			       struct liftoff_layer *layer)
{
	size_t i;

	/* TODO: speed this up with an array of bools indicating whether a layer
	 * has been allocated */
	for (i = 0; i < step->plane_idx; i++) {
		if (step->alloc[i] == layer) {
			return true;
		}
	}
	return false;
}

static bool has_composited_layer_over(struct liftoff_output *output,
				      struct alloc_step *step,
				      struct liftoff_layer *layer)
{
	struct liftoff_layer *other_layer;
	struct liftoff_layer_property *zpos_prop, *other_zpos_prop;

	zpos_prop = layer_get_property(layer, "zpos");
	if (zpos_prop == NULL) {
		return false;
	}

	liftoff_list_for_each(other_layer, &output->layers, link) {
		if (is_layer_allocated(step, other_layer)) {
			continue;
		}

		other_zpos_prop = layer_get_property(other_layer, "zpos");
		if (other_zpos_prop == NULL) {
			continue;
		}

		if (layer_intersects(layer, other_layer) &&
		    other_zpos_prop->value > zpos_prop->value) {
			return true;
		}
	}

	return false;
}

static bool has_allocated_layer_over(struct liftoff_output *output,
				     struct alloc_step *step,
				     struct liftoff_layer *layer)
{
	ssize_t i;
	struct liftoff_plane *other_plane;
	struct liftoff_layer *other_layer;
	struct liftoff_layer_property *zpos_prop, *other_zpos_prop;

	zpos_prop = layer_get_property(layer, "zpos");
	if (zpos_prop == NULL) {
		return false;
	}

	i = -1;
	liftoff_list_for_each(other_plane, &output->device->planes, link) {
		i++;
		if (i >= (ssize_t)step->plane_idx) {
			break;
		}
		if (other_plane->type == DRM_PLANE_TYPE_PRIMARY) {
			continue;
		}

		other_layer = step->alloc[i];
		if (other_layer == NULL) {
			continue;
		}

		other_zpos_prop = layer_get_property(other_layer, "zpos");
		if (other_zpos_prop == NULL) {
			continue;
		}

		/* Since plane zpos is descending, this means the other layer is
		 * supposed to be under but is mapped to a plane over the
		 * current one. */
		if (zpos_prop->value > other_zpos_prop->value &&
		    layer_intersects(layer, other_layer)) {
			return true;
		}
	}

	return false;
}

static bool has_allocated_plane_under(struct liftoff_output *output,
				      struct alloc_step *step,
				      struct liftoff_layer *layer)
{
	struct liftoff_plane *plane, *other_plane;
	ssize_t i;

	plane = liftoff_container_of(step->plane_link, plane, link);

	i = -1;
	liftoff_list_for_each(other_plane, &output->device->planes, link) {
		i++;
		if (i >= (ssize_t)step->plane_idx) {
			break;
		}
		if (other_plane->type == DRM_PLANE_TYPE_PRIMARY) {
			continue;
		}
		if (step->alloc[i] == NULL) {
			continue;
		}

		if (plane->zpos >= other_plane->zpos &&
		    layer_intersects(layer, step->alloc[i])) {
			return true;
		}
	}

	return false;
}

static bool check_layer_plane_compatible(struct alloc_step *step,
					 struct liftoff_layer *layer,
					 struct liftoff_plane *plane)
{
	struct liftoff_output *output;
	struct liftoff_layer_property *zpos_prop;

	output = layer->output;

	/* Skip this layer if already allocated */
	if (is_layer_allocated(step, layer)) {
		return false;
	}

	zpos_prop = layer_get_property(layer, "zpos");
	if (zpos_prop != NULL) {
		if ((int)zpos_prop->value > step->last_layer_zpos &&
		    has_allocated_layer_over(output, step, layer)) {
			/* This layer needs to be on top of the last
			 * allocated one */
			liftoff_log(LIFTOFF_DEBUG,
				    "%s Layer %p -> plane %"PRIu32": "
				    "layer zpos invalid",
				    step->log_prefix, (void *)layer, plane->id);
			return false;
		}
		if ((int)zpos_prop->value < step->last_layer_zpos &&
		    has_allocated_plane_under(output, step, layer)) {
			/* This layer needs to be under the last
			 * allocated one, but this plane isn't under the
			 * last one (in practice, since planes are
			 * sorted by zpos it means it has the same zpos,
			 * ie. undefined ordering). */
			liftoff_log(LIFTOFF_DEBUG,
				    "%s Layer %p -> plane %"PRIu32": "
				    "plane zpos invalid",
				    step->log_prefix, (void *)layer, plane->id);
			return false;
		}
	}

	if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
	    has_composited_layer_over(output, step, layer)) {
		liftoff_log(LIFTOFF_DEBUG,
			    "%s Layer %p -> plane %"PRIu32": "
			    "has composited layer on top",
			    step->log_prefix, (void *)layer, plane->id);
		return false;
	}

	if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
	    layer == layer->output->composition_layer) {
		liftoff_log(LIFTOFF_DEBUG,
			    "%s Layer %p -> plane %"PRIu32": "
			    "cannot put composition layer on "
			    "non-primary plane",
			    step->log_prefix, (void *)layer, plane->id);
		return false;
	}

	return true;
}

static bool check_alloc_valid(struct alloc_result *result,
			      struct alloc_step *step)
{
	/* If composition isn't used, we need to have allocated all
	 * layers. */
	/* TODO: find a way to fail earlier, e.g. when the number of
	 * layers exceeds the number of planes. */
	if (result->has_composition_layer && !step->composited &&
	    step->score != (int)result->non_composition_layers_len) {
		liftoff_log(LIFTOFF_DEBUG,
			    "%sCannot skip composition: some layers "
			    "are missing a plane", step->log_prefix);
		return false;
	}
	/* On the other hand, if we manage to allocate all layers, we
	 * don't want to use composition. We don't want to use the
	 * composition layer at all. */
	if (step->composited &&
	    step->score == (int)result->non_composition_layers_len) {
		liftoff_log(LIFTOFF_DEBUG,
			    "%sRefusing to use composition: all layers "
			    "have been put in a plane", step->log_prefix);
		return false;
	}

	/* TODO: check allocation isn't empty */

	return true;
}

static int output_choose_layers(struct liftoff_output *output,
				struct alloc_result *result,
				struct alloc_step *step)
{
	struct liftoff_device *device;
	struct liftoff_plane *plane;
	struct liftoff_layer *layer;
	int cursor, ret;
	size_t remaining_planes;
	struct alloc_step next_step = {0};

	device = output->device;

	if (step->plane_link == &device->planes) { /* Allocation finished */
		if (step->score > result->best_score &&
		    check_alloc_valid(result, step)) {
			/* We found a better allocation */
			liftoff_log(LIFTOFF_DEBUG,
				    "%sFound a better allocation with score=%d",
				    step->log_prefix, step->score);
			result->best_score = step->score;
			memcpy(result->best, step->alloc,
			       result->planes_len * sizeof(struct liftoff_layer *));
		}
		return 0;
	}

	plane = liftoff_container_of(step->plane_link, plane, link);

	remaining_planes = result->planes_len - step->plane_idx;
	if (result->best_score >= step->score + (int)remaining_planes) {
		/* Even if we find a layer for all remaining planes, we won't
		 * find a better allocation. Give up. */
		/* TODO: change remaining_planes to only count those whose
		 * possible CRTC match and which aren't allocated */
		return 0;
	}

	cursor = drmModeAtomicGetCursor(result->req);

	if (plane->layer != NULL) {
		goto skip;
	}
	if ((plane->possible_crtcs & (1 << output->crtc_index)) == 0) {
		goto skip;
	}

	liftoff_log(LIFTOFF_DEBUG,
		    "%sPerforming allocation for plane %"PRIu32" (%zu/%zu)",
		    step->log_prefix, plane->id, step->plane_idx + 1, result->planes_len);

	liftoff_list_for_each(layer, &output->layers, link) {
		if (layer->plane != NULL || layer->force_composition) {
			continue;
		}
		if (!layer_is_visible(layer)) {
			continue;
		}
		if (!check_layer_plane_compatible(step, layer, plane)) {
			continue;
		}

		/* Try to use this layer for the current plane */
		ret = plane_apply(plane, layer, result->req);
		if (ret == -EINVAL) {
			liftoff_log(LIFTOFF_DEBUG,
				    "%s Layer %p -> plane %"PRIu32": "
				    "incompatible properties",
				    step->log_prefix, (void *)layer, plane->id);
			continue;
		} else if (ret != 0) {
			return ret;
		}

		ret = device_test_commit(device, result->req, result->flags);
		if (ret == 0) {
			liftoff_log(LIFTOFF_DEBUG,
				    "%s Layer %p -> plane %"PRIu32": success",
				    step->log_prefix, (void *)layer, plane->id);
			/* Continue with the next plane */
			plane_step_init_next(&next_step, step, layer);
			ret = output_choose_layers(output, result, &next_step);
			if (ret != 0) {
				return ret;
			}
		} else if (ret != -EINVAL && ret != -ERANGE) {
			return ret;
		} else {
			liftoff_log(LIFTOFF_DEBUG,
				    "%s Layer %p -> plane %"PRIu32": "
				    "test-only commit failed",
				    step->log_prefix, (void *)layer, plane->id);
		}

		drmModeAtomicSetCursor(result->req, cursor);
	}

skip:
	/* Try not to use the current plane */
	plane_step_init_next(&next_step, step, NULL);
	ret = output_choose_layers(output, result, &next_step);
	if (ret != 0) {
		return ret;
	}
	drmModeAtomicSetCursor(result->req, cursor);

	return 0;
}

static int apply_current(struct liftoff_device *device, drmModeAtomicReq *req)
{
	struct liftoff_plane *plane;
	int cursor, ret;

	cursor = drmModeAtomicGetCursor(req);

	liftoff_list_for_each(plane, &device->planes, link) {
		ret = plane_apply(plane, plane->layer, req);
		assert(ret != -EINVAL);
		if (ret != 0) {
			drmModeAtomicSetCursor(req, cursor);
			return ret;
		}
	}

	return 0;
}

static bool layer_needs_realloc(struct liftoff_layer *layer)
{
	size_t i;
	struct liftoff_layer_property *prop;

	if (layer->changed) {
		return true;
	}

	for (i = 0; i < layer->props_len; i++) {
		prop = &layer->props[i];
		if (prop->value == prop->prev_value) {
			continue;
		}

		/* If FB_ID changes from non-zero to zero, we don't need to
		 * display this layer anymore, so we may be able to re-use its
		 * plane for another layer. If FB_ID changes from zero to
		 * non-zero, we might be able to find a plane for this layer.
		 * If FB_ID changes from non-zero to non-zero, we can try to
		 * re-use the previous allocation. */
		if (strcmp(prop->name, "FB_ID") == 0) {
			if (prop->value == 0 || prop->prev_value == 0) {
				return true;
			}
			/* TODO: check format/modifier is the same? */
			continue;
		}

		/* If the layer was or becomes completely transparent or
		 * completely opaque, we might be able to find a better
		 * allocation. Otherwise, we can keep the current one. */
		if (strcmp(prop->name, "alpha") == 0) {
			if (prop->value == 0 || prop->prev_value == 0 ||
			    prop->value == 0xFFFF || prop->prev_value == 0xFFFF) {
				return true;
			}
			continue;
		}

		/* We should never need a re-alloc when IN_FENCE_FD or
		 * FB_DAMAGE_CLIPS changes. */
		if (strcmp(prop->name, "IN_FENCE_FD") == 0 ||
		    strcmp(prop->name, "FB_DAMAGE_CLIPS") == 0) {
			continue;
		}

		/* TODO: if CRTC_{X,Y,W,H} changed but intersection with other
		 * layers hasn't changed, don't realloc */
		return true;
	}

	return false;
}

static int reuse_previous_alloc(struct liftoff_output *output,
				drmModeAtomicReq *req, uint32_t flags)
{
	struct liftoff_device *device;
	struct liftoff_layer *layer;
	int cursor, ret;

	device = output->device;

	if (output->layers_changed) {
		return -EINVAL;
	}

	liftoff_list_for_each(layer, &output->layers, link) {
		if (layer_needs_realloc(layer)) {
			return -EINVAL;
		}
	}

	cursor = drmModeAtomicGetCursor(req);

	ret = apply_current(device, req);
	if (ret != 0) {
		return ret;
	}

	ret = device_test_commit(device, req, flags);
	if (ret != 0) {
		drmModeAtomicSetCursor(req, cursor);
	}
	return ret;
}

static void mark_layers_clean(struct liftoff_output *output)
{
	struct liftoff_layer *layer;

	output->layers_changed = false;

	liftoff_list_for_each(layer, &output->layers, link) {
		layer_mark_clean(layer);
	}
}

static void update_layers_priority(struct liftoff_device *device)
{
	struct liftoff_output *output;
	struct liftoff_layer *layer;

	device->page_flip_counter++;
	bool period_elapsed =
		device->page_flip_counter >= LIFTOFF_PRIORITY_PERIOD;
	if (period_elapsed) {
		device->page_flip_counter = 0;
	}

	liftoff_list_for_each(output, &device->outputs, link) {
		liftoff_list_for_each(layer, &output->layers, link) {
			layer_update_priority(layer, period_elapsed);
		}
	}
}

static void log_reuse(struct liftoff_output *output)
{
	if (output->alloc_reused_counter == 0) {
		liftoff_log(LIFTOFF_DEBUG,
			    "Reusing previous plane allocation on output %p",
			    (void *)output);
	}
	output->alloc_reused_counter++;
}

static void log_no_reuse(struct liftoff_output *output)
{
	liftoff_log(LIFTOFF_DEBUG, "Computing plane allocation on output %p",
		    (void *)output);

	if (output->alloc_reused_counter != 0) {
		liftoff_log(LIFTOFF_DEBUG,
			    "Stopped reusing previous plane allocation on "
			    "output %p (had reused it %d times)",
			    (void *)output, output->alloc_reused_counter);
		output->alloc_reused_counter = 0;
	}
}

static size_t non_composition_layers_length(struct liftoff_output *output)
{
	struct liftoff_layer *layer;
	size_t n;

	n = 0;
	liftoff_list_for_each(layer, &output->layers, link) {
		if (layer_is_visible(layer) &&
		    output->composition_layer != layer) {
			n++;
		}
	}

	return n;
}

int liftoff_output_apply(struct liftoff_output *output, drmModeAtomicReq *req,
			 uint32_t flags)
{
	struct liftoff_device *device;
	struct liftoff_plane *plane;
	struct liftoff_layer *layer;
	struct alloc_result result = {0};
	struct alloc_step step = {0};
	size_t i, candidate_planes;
	int ret;

	device = output->device;

	update_layers_priority(device);

	ret = reuse_previous_alloc(output, req, flags);
	if (ret == 0) {
		log_reuse(output);
		return 0;
	}
	log_no_reuse(output);

	output_log_layers(output);

	/* Unset all existing plane and layer mappings. */
	liftoff_list_for_each(plane, &device->planes, link) {
		if (plane->layer != NULL && plane->layer->output == output) {
			plane->layer->plane = NULL;
			plane->layer = NULL;
		}
	}

	/* Disable all planes we might use. Do it before building mappings to
	 * make sure not to hit bandwidth limits because too many planes are
	 * enabled. */
	candidate_planes = 0;
	liftoff_list_for_each(plane, &device->planes, link) {
		if (plane->layer == NULL) {
			candidate_planes++;
			liftoff_log(LIFTOFF_DEBUG,
				    "Disabling plane %"PRIu32, plane->id);
			ret = plane_apply(plane, NULL, req);
			assert(ret != -EINVAL);
			if (ret != 0) {
				return ret;
			}
		}
	}

	result.req = req;
	result.flags = flags;
	result.planes_len = liftoff_list_length(&device->planes);

	step.alloc = malloc(result.planes_len * sizeof(*step.alloc));
	result.best = malloc(result.planes_len * sizeof(*result.best));
	if (step.alloc == NULL || result.best == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "malloc");
		return -ENOMEM;
	}

	/* For each plane, try to find a layer. Don't do it the other
	 * way around (ie. for each layer, try to find a plane) because
	 * some drivers want user-space to enable the primary plane
	 * before any other plane. */

	result.best_score = -1;
	memset(result.best, 0, result.planes_len * sizeof(*result.best));
	result.has_composition_layer = output->composition_layer != NULL;
	result.non_composition_layers_len =
		non_composition_layers_length(output);
	step.plane_link = device->planes.next;
	step.plane_idx = 0;
	step.score = 0;
	step.last_layer_zpos = INT_MAX;
	step.composited = false;
	ret = output_choose_layers(output, &result, &step);
	if (ret != 0) {
		return ret;
	}

	liftoff_log(LIFTOFF_DEBUG,
		    "Found plane allocation for output %p (score: %d, candidate planes: %zu):",
		    (void *)output, result.best_score, candidate_planes);

	/* Apply the best allocation */
	i = 0;
	liftoff_list_for_each(plane, &device->planes, link) {
		layer = result.best[i];
		i++;
		if (layer == NULL) {
			continue;
		}

		liftoff_log(LIFTOFF_DEBUG, "  Layer %p -> plane %"PRIu32,
			    (void *)layer, plane->id);

		assert(plane->layer == NULL);
		assert(layer->plane == NULL);
		plane->layer = layer;
		layer->plane = plane;
	}
	if (i == 0) {
		liftoff_log(LIFTOFF_DEBUG, "  (No layer has a plane)");
	}

	ret = apply_current(device, req);
	if (ret != 0) {
		return ret;
	}

	free(step.alloc);
	free(result.best);

	mark_layers_clean(output);

	return 0;
}
