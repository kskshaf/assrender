#include "assrender.h"
#include "render.h"
#include "sub.h"

#if defined(_WIN32)
#    define CSRIAPI __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#    define CSRIAPI __attribute__((visibility("default")))
#else
#    define CSRIAPI
#endif
#define CSRI_OWN_HANDLES
typedef const char *csri_rend;
typedef struct {
    udata *ud;
    int64_t width, height;
    bool frame_requested;
} csri_inst;

#include "csri.h"

static csri_rend csri_assrender = "assrender";

CSRIAPI csri_inst *csri_open_file(csri_rend *renderer, const char *filename, struct csri_openflag *flags)
{
    csri_inst *inst = malloc(sizeof(csri_inst));
    memset(inst, 0, sizeof(csri_inst));
    inst->ud = malloc(sizeof(udata));
    memset(inst->ud, 0, sizeof(udata));

    if (init_ass(0, 0, 1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", inst->ud)) {
        ASS_Track *ass = ass_read_file(inst->ud->ass_library, filename, (char *)"utf-8");
        if (ass) {
            inst->ud->ass = ass;

            inst->ud->f_make_sub_img = make_sub_img;

            return inst;
        }
    }
    csri_close(inst);
    return NULL;
}


CSRIAPI csri_inst *csri_open_mem(csri_rend *renderer, const void *data, size_t length, struct csri_openflag *flags)
{
    csri_inst *inst = malloc(sizeof(csri_inst));
    memset(inst, 0, sizeof(csri_inst));
    inst->ud = malloc(sizeof(udata));
    memset(inst->ud, 0, sizeof(udata));

    if (init_ass(0, 0, 1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", inst->ud)) {
        ASS_Track *ass = ass_read_memory(inst->ud->ass_library, data, length, (char *)"utf-8");
        if (ass) {
            inst->ud->ass = ass;

            inst->ud->f_make_sub_img = make_sub_img;

            return inst;
        }
    }
    csri_close(inst);
    return NULL;
}


CSRIAPI void csri_close(csri_inst *inst)
{
    if (!inst) return;

    udata *ud = inst->ud;

    ass_renderer_done(ud->ass_renderer);
    ass_library_done(ud->ass_library);
    ass_free_track(ud->ass);

    if (inst->frame_requested) {
        for (int i = 0; i < 4; ++i) {
            free(ud->sub_img[i]);
        }
    }

    if (ud->isvfr)
        free(ud->timestamp);

    free(ud);
    free(inst);
}


CSRIAPI int csri_request_fmt(csri_inst *inst, const struct csri_fmt *fmt)
{
    if (!inst) return -1;

    if (!fmt->width || !fmt->height)
        return -1;

    switch (fmt->pixfmt) {
    case CSRI_F_BGR_:
        inst->ud->apply = apply_rgba;
        break;
    default:
        return -1;
    }

    if (inst->frame_requested && (inst->width != fmt->width || inst->height != fmt->height)) {
        for (int i = 0; i < 4; ++i) {
            free(inst->ud->sub_img[i]);
        }
        inst->frame_requested = false;
    }

    inst->width = fmt->width;
    inst->height = fmt->height;

    FillMatrix(&inst->ud->mx, MATRIX_NONE);

    ass_set_frame_size(inst->ud->ass_renderer, inst->width, inst->height);
    ass_set_storage_size(inst->ud->ass_renderer, inst->width, inst->height);

    const int bits_per_pixel = 8;
    const int pixelsize = 32;
    const int rgb_fullscale = false;
    const int greyscale = false;

    const int buffersize = inst->width * inst->height * pixelsize;

    if (!inst->frame_requested) {
        for (int i = 0; i < 4; ++i) {
            inst->ud->sub_img[i] = malloc(buffersize);
        }
        inst->frame_requested = true;
    }

    inst->ud->bits_per_pixel = bits_per_pixel;
    inst->ud->pixelsize = pixelsize;
    inst->ud->rgb_fullscale = rgb_fullscale;
    inst->ud->greyscale = greyscale;

    return 0;
}


CSRIAPI void csri_render(csri_inst *inst, struct csri_frame *frame, double time)
{
    int changed;
    long long ts = time * 1000;
    ASS_Image *img = ass_render_frame(inst->ud->ass_renderer, inst->ud->ass, ts, &changed);

    if (img) {
        uint32_t height, width, pitch[2];
        uint8_t *data[3];

        switch (frame->pixfmt) {
        case CSRI_F_BGR_:
            data[0] = frame->planes[0];
            pitch[0] = frame->strides[0];
            break;
        default:
            return;
        }

        height = inst->height;
        width = inst->width;
        
        if (changed) {
            memset(inst->ud->sub_img[0], 0x00, height * width * inst->ud->pixelsize);
            inst->ud->f_make_sub_img(img, inst->ud->sub_img, width, inst->ud->bits_per_pixel, inst->ud->rgb_fullscale, &inst->ud->mx);
        }

        inst->ud->apply(inst->ud->sub_img, data, pitch, width, height);
    }
}


// No extensions supported
CSRIAPI void *csri_query_ext(csri_rend *rend, csri_ext_id extname)
{
    return 0;
}

// Get info for renderer
static struct csri_info csri_assrender_info = {
#ifdef _DEBUG
    .name = "assrender_textsub_debug", // name
    .specific = "0.37.1", // version (assumed version number, svn revision, patchlevel)
#else
    .name = "assrender_textsub", // name
    .specific = "0.37.1", // version (assumed version number, svn revision, patchlevel)
#endif
    .longname = "assrender/TextSub (pingplug & pinterf & Masaiki)", // longname
    .author = "pingplug", // author
    .copyright = "Copyright (c) 2012-2015 by pingplug, 2021 by pinterf, 2021-2022 by Masaiki" // copyright
};
CSRIAPI struct csri_info *csri_renderer_info(csri_rend *rend)
{
    return &csri_assrender_info;
}
// Only one supported, obviously
CSRIAPI csri_rend *csri_renderer_byname(const char *name, const char *specific)
{
    if (strcmp(name, csri_assrender_info.name))
        return 0;
    if (specific && strcmp(specific, csri_assrender_info.specific))
        return 0;
    return &csri_assrender;
}
// Still just one
CSRIAPI csri_rend *csri_renderer_default()
{
    return &csri_assrender;
}
// And no further
CSRIAPI csri_rend *csri_renderer_next(csri_rend *prev)
{
    return 0;
}

