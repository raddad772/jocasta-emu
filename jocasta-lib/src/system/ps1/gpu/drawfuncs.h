template<bool semi_transparent, bool textured, bool modulated>
void draw_rect(RT_POINT2D *v0, RT_POINT2D *v1, u32 color) {
    RT_POINT2D p;
    i64 r_mul, g_mul, b_mul;
    i64 mr, mg, mb;
    u32 tcol, tmask=0;;
    i32 u,v;

    i32 u_increment = rect.texture_x_flip ? -1 : 1;
    i32 v_increment = rect.texture_y_flip ? -1 : 1;
    if (((v1->x - v0->x) > 1023) || ((v0->y - v1->y) > 511)) return;

    if (v0->x < draw_area_left) {
        if constexpr (textured)
            v0->u = (v0->u + (draw_area_left - v0->x)) & 255;
        v0->x = draw_area_left;
    }
    if (v0->y < draw_area_top) {
        if constexpr (textured)
            v0->v = (v0->v + (draw_area_top - v0->y)) & 255;
        v0->y = draw_area_top;
    }
    if (v1->x > draw_area_right) v1->x = draw_area_right;
    if (v1->y > draw_area_bottom) v1->y = draw_area_bottom;

    if constexpr (textured) v = v0->v;
    if constexpr (textured && modulated) {
        r_mul = (color & 0xFF) << 1;
        g_mul = (color >> 7) & 0x1FE;
        b_mul = (color >> 15) & 0x1FE;
    }
    if constexpr (!textured) {
        r_mul = (color & 0xFF);
        g_mul = (color >> 8) & 0xFF;
        b_mul = (color >> 16) & 0xFF;
    }
    for (p.y = v0->y; p.y < v1->y; p.y++) {
        if constexpr (textured) u = v0->u;
        for (p.x = v0->x; p.x < v1->x; p.x++) {
            if constexpr(textured) {
                tcol = (this->*gts.sample)(u, v);
                if (tcol == 0) {
                    u = (u + u_increment) & 255;
                    continue;
                }
                tmask = tcol & 0x8000;
                mr = (tcol & 0x1F);
                mg = (tcol >> 5) & 0x1F;
                mb = (tcol >> 10) & 0x1F;

                if constexpr (modulated) {
                    mr = (mr * r_mul) >> 5;
                    mg = (mg * g_mul) >> 5;
                    mb = (mb * b_mul) >> 5;
                }
                else {
                    mr <<= 3;
                    mg <<= 3;
                    mb <<= 3;
                }
            }
            else { // ! textured
                mr = r_mul;
                mg = g_mul;
                mb = b_mul;
            }
            mr = CLAMP(mr, 0, 255) >> 3;
            mg = CLAMP(mg, 0, 255) >> 3;
            mb = CLAMP(mb, 0, 255) >> 3;

            if constexpr(semi_transparent) {
                semipix_split(p.y, p.x, mr, mg, mb, tmask, !textured);
            }
            else {
                setpix_split(p.y, p.x, mr, mg, mb, tmask);
            }

            if constexpr (textured) u = (u + u_increment) & 255;
        }
        if constexpr (textured) v = (v + v_increment) & 255;
    }
}

template<bool semi_transparent, bool goraud>
void draw_line(RT_POINT2D *v0, RT_POINT2D *v1, u32 first_color) {
    i32 x0 = v0->x;
    i32 y0 = v0->y;
    i32 x1 = v1->x;
    i32 y1 = v1->y;

    i32 dx = std::abs(x1 - x0);
    i32 dy = std::abs(y1 - y0);
    i32 sx = (x0 < x1) ? 1 : -1;
    i32 sy = (y0 < y1) ? 1 : -1;
    i32 err = dx - dy;

    const i32 steps = std::max(dx, dy);

    i32 r0, g0, b0;
    i32 r1, g1, b1;

    if constexpr (goraud) {
        r0 = static_cast<i32>(v0->r);
        g0 = static_cast<i32>(v0->g);
        b0 = static_cast<i32>(v0->b);

        r1 = static_cast<i32>(v1->r);
        g1 = static_cast<i32>(v1->g);
        b1 = static_cast<i32>(v1->b);
    } else {
        r0 = r1 = static_cast<i32>((first_color >> 0)  & 0xFF);
        g0 = g1 = static_cast<i32>((first_color >> 8)  & 0xFF);
        b0 = b1 = static_cast<i32>((first_color >> 16) & 0xFF);
    }

    RT_POINT2D p;
    i32 step = 0;

    for (;;) {
        p.x = x0;
        p.y = y0;

        i32 mr, mg, mb;

        if constexpr (goraud) {
            if (steps == 0) {
                mr = r0;
                mg = g0;
                mb = b0;
            } else {
                // Exact endpoints:
                // step == 0      -> v0 color
                // step == steps  -> v1 color
                mr = r0 + ((r1 - r0) * step) / steps;
                mg = g0 + ((g1 - g0) * step) / steps;
                mb = b0 + ((b1 - b0) * step) / steps;
            }
        } else {
            mr = r0;
            mg = g0;
            mb = b0;
        }

        u32 caddr = ditherP(p);
        mr += dithertable[caddr];
        mg += dithertable[caddr];
        mb += dithertable[caddr];

        mr = CLAMP(mr, 0, 255) >> 3;
        mg = CLAMP(mg, 0, 255) >> 3;
        mb = CLAMP(mb, 0, 255) >> 3;

        if constexpr (semi_transparent) {
            semipix_split(p.y, p.x, mr, mg, mb, 0, true);
        } else {
            setpix_split(p.y, p.x, mr, mg, mb, 0);
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        i32 e2 = err << 1;

        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }

        ++step;
    }
}

template<bool semi_transparent,  bool goraud, bool textured, bool dither>
void draw_tri(RT_POINT2D *v0, RT_POINT2D *v1, RT_POINT2D *v2, u32 first_color) {
    i32 minX = MIN3(v0->x, v1->x, v2->x);
    i32 minY = MIN3(v0->y, v1->y, v2->y);
    i32 maxX = MAX3(v0->x, v1->x, v2->x);
    i32 maxY = MAX3(v0->y, v1->y, v2->y);
    if (((maxY - minY) > 511) || ((maxX - minX) > 1023)) return;
    minX = MAX(minX, draw_area_left);
    maxX = MIN(maxX, draw_area_right);
    minY = MAX(minY, draw_area_top);
    maxY = MIN(maxY, draw_area_bottom);
    if (minX > maxX || minY > maxY) return;
    i64 r_mul, g_mul, b_mul, u, v;
    i64 mr, mg, mb;
    u32 tmask = 0;
    u32 tcol;

    i64 cross_product_z = cpz(v0, v1, v2);
    if (cross_product_z < 0) {
        RT_POINT2D *sa = v0;
        v0 = v1;
        v1 = sa;
        cross_product_z = cpz(v0, v1, v2);
    }

    if constexpr (!goraud) { // these are calculated during run if goraud
        if constexpr (textured) {
            r_mul = (static_cast<i64>(first_color) & 0xFF) << 1;
            g_mul = (static_cast<i64>(first_color >> 8) & 0xFF) << 1;
            b_mul = (static_cast<i64>(first_color >> 16) & 0xFF) << 1;
        }
        else {
            r_mul = (static_cast<i64>(first_color) & 0xFF);
            g_mul = (static_cast<i64>(first_color >> 8) & 0xFF);
            b_mul = (static_cast<i64>(first_color >> 16) & 0xFF);
        }
    }

    // Initialise our point
    RT_POINT2D p;
    i64 lambda[3];
    for (p.y = minY; p.y < maxY; p.y++) {
        for (p.x = minX; p.x < maxX; p.x++) {
            if (is_inside_triangle(&p, v0, v1, v2)) {
                compute_barycentric(cross_product_z, &p, v0, v1, v2, lambda);

                if constexpr (textured) {
                    u = ((lambda[0] * v0->u) + (lambda[1] * v1->u) + (lambda[2] * v2->u)) >> 32;
                    v = ((lambda[0] * v0->v) + (lambda[1] * v1->v) + (lambda[2] * v2->v)) >> 32;
                    tcol = (this->*gts.sample)(static_cast<i32>(u) & 0xFF, static_cast<i32>(v) & 0xFF);
                    if (tcol == 0) continue;
                }
                if constexpr (goraud) {
                    r_mul = ((lambda[0] * v0->r) + (lambda[1] * v1->r) + (lambda[2] * v2->r)) >> 32;
                    g_mul = ((lambda[0] * v0->g) + (lambda[1] * v1->g) + (lambda[2] * v2->g)) >> 32;
                    b_mul = ((lambda[0] * v0->b) + (lambda[1] * v1->b) + (lambda[2] * v2->b)) >> 32;
                    if constexpr (textured) {
                        r_mul <<= 1;
                        g_mul <<= 1;
                        b_mul <<= 1;
                    }
                }

                if constexpr(textured) {
                    tmask = tcol & 0x8000;
                    mr = (tcol & 0x1F);
                    mg = (tcol >> 5) & 0x1F;
                    mb = (tcol >> 10) & 0x1F;

                    mr = (mr * r_mul) >> 5; //
                    mg = (mg * g_mul) >> 5;
                    mb = (mb * b_mul) >> 5;
                }
                else {
                    mr = r_mul;
                    mg = g_mul;
                    mb = b_mul;
                }

                if constexpr (goraud && dither) {
                    u32 caddr = ditherP(p);
                    mr += dithertable[caddr];
                    mg += dithertable[caddr];
                    mb += dithertable[caddr];
                }
                mr = CLAMP(mr, 0, 255) >> 3;
                mg = CLAMP(mg, 0, 255) >> 3;
                mb = CLAMP(mb, 0, 255) >> 3;

                if constexpr(semi_transparent) {
                    semipix_split(p.y, p.x, mr, mg, mb, tmask, !textured);
                }
                else {
                    setpix_split(p.y, p.x, mr, mg, mb, tmask);
                }
            }
        }
    }
}
