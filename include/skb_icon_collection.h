// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_ICON_COLLECTION_H
#define SKB_ICON_COLLECTION_H

#include "skb_canvas.h"
#include "skb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup icon_collection Icon Collection
 *
 * An Icon Collection contains number of icons, which can be queried based by name.
 *
 * The collection supports icons in PicoSVG format. It is a tiny SVG subset, see https://github.com/googlefonts/picosvg
 *
 * Alternatively the icons can be created in code.
 * @{
 */

/** Opaque type for the icon collection. Use skb_icon_collection_create() to create. */
typedef struct skb_icon_collection_t skb_icon_collection_t;

/** Opaque type for the icon. Use skb_icon_collection_add_icon() to create. */
typedef struct skb_icon_t skb_icon_t;

/** Opaque type for the icon shape. Use skb_icon_shape_add() to create. */
typedef struct skb_icon_shape_t skb_icon_shape_t;


/**
 * Create new icon collection.
 * @return create icon collection.
 */
skb_icon_collection_t* skb_icon_collection_create(void);

/**
 * Destroy icon collection.
 * @param icon_collection icon collection to destroy.
 */
void skb_icon_collection_destroy(skb_icon_collection_t* icon_collection);

/**
 * Adds PicoSVG to the icon collection.
 * @param icon_collection icon collection to use.
 * @param name name of the icon (used for querying).
 * @param file_name name of the icon to add.
 * @return pointer to the added icon, or NULL if failed.
 */
skb_icon_t* skb_icon_collection_add_picosvg_icon(skb_icon_collection_t* icon_collection, const char* name, const char* file_name);

/**
 * Adds an empty icon of specified name and size.
 * @param icon_collection icon collection to use.
 * @param name name of the icon (used for querying).
 * @param width width of the icon to create.
 * @param height height of the icon to create.
 * @return pointer to the added icon, or NULL if failed.
 */
skb_icon_t* skb_icon_collection_add_icon(skb_icon_collection_t* icon_collection, const char* name, float width, float height);

/**
 * Finds an icon based on name.
 * @param icon_collection collection to use.
 * @param name name of the icon to query.
 * @return pointer to the icon, or NULL if not found.
 */
skb_icon_t* skb_icon_collection_find_icon(skb_icon_collection_t* icon_collection, const char* name);

/** @return size of the icon. */
skb_vec2_t skb_icon_get_size(skb_icon_t* icon);

/**
 * Adds shape to specified icon.
 * @param icon icon to add to.
 * @return pointer to the new shape.
 */
skb_icon_shape_t* skb_icon_add_shape(skb_icon_t* icon);

/**
 * Adds shape child shape to specified shape.
 * @param parent_shape shape to add to.
 * @return pointer to the new shape.
 */
skb_icon_shape_t* skb_icon_shape_add_child(skb_icon_shape_t* parent_shape);

/**
 * Adds new path to the shape.
 * @param shape shape to add to.
 * @param pt start position.
 */
void skb_icon_shape_move_to(skb_icon_shape_t* shape, skb_vec2_t pt);

/**
 * Appends line to the current path.
 * @param shape shape to add to.
 * @param pt line end position.
 */
void skb_icon_shape_line_to(skb_icon_shape_t* shape, skb_vec2_t pt);

/**
 * Appends quadratic bezier segment to the current path.
 * @param shape sape to add to.
 * @param cp control point of the curve.
 * @param pt curve end position.
 */
void skb_icon_shape_quad_to(skb_icon_shape_t* shape, skb_vec2_t cp, skb_vec2_t pt);

/**
 * Appends cubic bezier segment to the current path.
 * @param shape shape to add to.
 * @param cp0 first control point of the curve.
 * @param cp1 second control point of the curve.
 * @param pt curve end position.
 */
void skb_icon_shape_cubic_to(skb_icon_shape_t* shape, skb_vec2_t cp0, skb_vec2_t cp1, skb_vec2_t pt);

/**
 * Closes the current path
 * @param shape path to add to.
 */
void skb_icon_shape_close_path(skb_icon_shape_t* shape);

/**
 * Sets the opacity of specified shape.
 * @param shape shape to change.
 * @param opacity opacity in range [0..1]
 */
void skb_icon_shape_set_opacity(skb_icon_shape_t* shape, float opacity);

/**
 * Sets the color of the specified shape.
 * @param shape shape to change.
 * @param color new color.
 */
void skb_icon_shape_set_color(skb_icon_shape_t* shape, skb_color_t color);

/**
 * Sets the gradient of specifid shape.
 * See skb_icon_create_*_gradient() to create gradients.
 * @param shape shape to change.
 * @param gradient_idx index of the gradient to set.
 */
void skb_icon_shape_set_gradient(skb_icon_shape_t* shape, int32_t gradient_idx);

/**
 * Creates new linear gradient to be used in an icon.
 * @param icon icon to use.
 * @param p0 start point of the gradient.
 * @param p1 end point of the gradient.
 * @param xform gradient transform.
 * @param spread spread mode of the gradient.
 * @param stops color stops for the gradient.
 * @param stops_count number of color stops.
 * @return index to the creatd gradient.
 */
int32_t skb_icon_create_linear_gradient(skb_icon_t* icon, skb_vec2_t p0, skb_vec2_t p1, skb_mat2_t xform, uint8_t spread, skb_color_stop_t* stops, int32_t stops_count);

/**
 * Creates new radial gradient to be used in an icon.
 * @param icon icon to use.
 * @param p0 start point of the gradient.
 * @param p1 end point of the gradient.
 * @param radius radius of the gradient at start point.
 * @param xform gradient transform.
 * @param spread spread mode of the gradient.
 * @param stops color stops for the gradient.
 * @param stops_count number of color stops.
 * @return index to the creatd gradient.
 */
int32_t skb_icon_create_radial_gradient(skb_icon_t* icon, skb_vec2_t p0, skb_vec2_t p1, float radius, skb_mat2_t xform, uint8_t spread, skb_color_stop_t* stops, int32_t stops_count);

/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_ICON_COLLECTION_H
