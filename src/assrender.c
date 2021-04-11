#include "assrender.h"
#include "render.h"
#include "sub.h"
#include "timecodes.h"

void AVSC_CC assrender_destroy(void* ud, AVS_ScriptEnvironment* env)
{
    ass_renderer_done(((udata*)ud)->ass_renderer);
    ass_library_done(((udata*)ud)->ass_library);
    ass_free_track(((udata*)ud)->ass);

    free(((udata*)ud)->sub_img[0]);
    free(((udata*)ud)->sub_img[1]);
    free(((udata*)ud)->sub_img[2]);
    free(((udata*)ud)->sub_img[3]);

    if (((udata*)ud)->isvfr)
        free(((udata*)ud)->timestamp);

    free(ud);
}

AVS_Value AVSC_CC assrender_create(AVS_ScriptEnvironment* env, AVS_Value args,
                                   void* ud)
{
    AVS_Value v;
    AVS_FilterInfo* fi;
    AVS_Clip* c = avs_new_c_filter(env, &fi, avs_array_elt(args, 0), 1);
    char e[250];

    const char* f = avs_as_string(avs_array_elt(args, 1));
    const char* vfr = avs_as_string(avs_array_elt(args, 2));
    int h = avs_is_int(avs_array_elt(args, 3)) ?
            avs_as_int(avs_array_elt(args, 3)) : 0;
    double scale = avs_is_float(avs_array_elt(args, 4)) ?
                   avs_as_float(avs_array_elt(args, 4)) : 1.0;
    double line_spacing = avs_is_float(avs_array_elt(args, 5)) ?
                          avs_as_float(avs_array_elt(args, 5)) : 0;
    double dar = avs_is_float(avs_array_elt(args, 6)) ?
                 avs_as_float(avs_array_elt(args, 6)) : 0;
    double sar = avs_is_float(avs_array_elt(args, 7)) ?
                 avs_as_float(avs_array_elt(args, 7)) : 0;
    int top = avs_is_int(avs_array_elt(args, 8)) ?
              avs_as_int(avs_array_elt(args, 8)) : 0;
    int bottom = avs_is_int(avs_array_elt(args, 9)) ?
                 avs_as_int(avs_array_elt(args, 9)) : 0;
    int left = avs_is_int(avs_array_elt(args, 10)) ?
               avs_as_int(avs_array_elt(args, 10)) : 0;
    int right = avs_is_int(avs_array_elt(args, 11)) ?
                avs_as_int(avs_array_elt(args, 11)) : 0;
    const char* cs = avs_as_string(avs_array_elt(args, 12)) ?
                     avs_as_string(avs_array_elt(args, 12)) : "UTF-8";
    int debuglevel = avs_is_int(avs_array_elt(args, 13)) ?
                     avs_as_int(avs_array_elt(args, 13)) : 0;
    const char* fontdir = avs_as_string(avs_array_elt(args, 14)) ?
#ifdef AVS_WINDOWS
        avs_as_string(avs_array_elt(args, 14)) : "C:/Windows/Fonts";
#else
        avs_as_string(avs_array_elt(args, 14)) : "/usr/share/fonts";
#endif
    const char* srt_font = avs_as_string(avs_array_elt(args, 15)) ?
                           avs_as_string(avs_array_elt(args, 15)) : "sans-serif";
    const char* colorspace = avs_as_string(avs_array_elt(args, 16)) ?
                             avs_as_string(avs_array_elt(args, 16)) : "";

    char* tmpcsp = calloc(1, BUFSIZ);
    strncpy(tmpcsp, colorspace, BUFSIZ - 1);

    ASS_Hinting hinting;
    udata* data;
    ASS_Track* ass;

    /*
    no unsupported colorspace left, bitness is checked at other place
    if (0 == 1) {
        v = avs_new_value_error(
                "AssRender: unsupported colorspace");
        avs_release_clip(c);
        return v;
    }
    */

    if (!f) {
        v = avs_new_value_error("AssRender: no input file specified");
        avs_release_clip(c);
        return v;
    }

    switch (h) {
    case 0:
        hinting = ASS_HINTING_NONE;
        break;
    case 1:
        hinting = ASS_HINTING_LIGHT;
        break;
    case 2:
        hinting = ASS_HINTING_NORMAL;
        break;
    case 3:
        hinting = ASS_HINTING_NATIVE;
        break;
    default:
        v = avs_new_value_error("AssRender: invalid hinting mode");
        avs_release_clip(c);
        return v;
    }

    data = malloc(sizeof(udata));

    if (!init_ass(fi->vi.width, fi->vi.height, scale, line_spacing, dar, sar,
                  top, bottom, left, right, hinting, debuglevel, fontdir,
                  data)) {
        v = avs_new_value_error("AssRender: failed to initialize");
        avs_release_clip(c);
        return v;
    }

    if (!strcasecmp(strrchr(f, '.'), ".srt"))
        ass = parse_srt(f, data, srt_font);
    else {
        ass = ass_read_file(data->ass_library, (char*)f, (char*)cs);
        ass_read_matrix(f, tmpcsp);
    }

    if (!ass) {
        sprintf(e, "AssRender: unable to parse '%s'", f);
        v = avs_new_value_error(e);
        avs_release_clip(c);
        return v;
    }

    data->ass = ass;

    if (vfr) {
        int ver;
        FILE* fh = fopen(vfr, "r");

        if (!fh) {
            sprintf(e, "AssRender: could not read timecodes file '%s'", vfr);
            v = avs_new_value_error(e);
            avs_release_clip(c);
            return v;
        }

        data->isvfr = 1;

        if (fscanf(fh, "# timecode format v%d", &ver) != 1) {
            sprintf(e, "AssRender: invalid timecodes file '%s'", vfr);
            v = avs_new_value_error(e);
            avs_release_clip(c);
            return v;
        }

        switch (ver) {
        case 1:

            if (!parse_timecodesv1(fh, fi->vi.num_frames, data)) {
                v = avs_new_value_error(
                        "AssRender: error parsing timecodes file");
                avs_release_clip(c);
                return v;
            }

            break;
        case 2:

            if (!parse_timecodesv2(fh, fi->vi.num_frames, data)) {
                v = avs_new_value_error(
                        "AssRender: timecodes file had less frames than "
                        "expected");
                avs_release_clip(c);
                return v;
            }

            break;
        }

        fclose(fh);
    } else {
        data->isvfr = 0;
    }

    matrix_type color_mt;

    if (avs_is_rgb(&fi->vi)) {
      color_mt = MATRIX_NONE; // no RGB->YUV conversion
    } else {
        // .ASS "YCbCr Matrix" valid values are
        // "none" "tv.601" "pc.601" "tv.709" "pc.709" "tv.240m" "pc.240m" "tv.fcc" "pc.fcc"
      if (!strcasecmp(tmpcsp, "bt.709") || !strcasecmp(tmpcsp, "rec709") || !strcasecmp(tmpcsp, "tv.709")) {
        color_mt = MATRIX_BT709;
      }
      else if (!strcasecmp(tmpcsp, "pc.709")) {
        color_mt = MATRIX_PC709;
      }
      else if (!strcasecmp(tmpcsp, "bt.601") || !strcasecmp(tmpcsp, "rec601") || !strcasecmp(tmpcsp, "tv.601")) {
        color_mt = MATRIX_BT601;
      }
      else if (!strcasecmp(tmpcsp, "pc.601")) {
        color_mt = MATRIX_PC601;
      }
      else if (!strcasecmp(tmpcsp, "tv.fcc")) {
        color_mt = MATRIX_TVFCC;
      }
      else if (!strcasecmp(tmpcsp, "pc.fcc")) {
        color_mt = MATRIX_PCFCC;
      }
      else if (!strcasecmp(tmpcsp, "tv.240m")) {
        color_mt = MATRIX_TV240M;
      }
      else if (!strcasecmp(tmpcsp, "pc.240m")) {
        color_mt = MATRIX_PC240M;
      }
      else if (!strcasecmp(tmpcsp, "bt.2020") || !strcasecmp(tmpcsp, "rec2020")) {
        color_mt = MATRIX_BT2020;
      }
      else if (!strcasecmp(tmpcsp, "none") || !strcasecmp(tmpcsp, "guess")) {
        /* not yet
        * Theoretically only for 10 and 12 bits:
        if (fi->vi.width > 1920 || fi->vi.height > 1080)
          color_mt = MATRIX_BT2020;
        else 
        */
        if (fi->vi.width > 1280 || fi->vi.height > 576)
          color_mt = MATRIX_PC709;
        else
          color_mt = MATRIX_PC601;
      }
      else {
        color_mt = MATRIX_BT601;
      }
    }

    FillMatrix(&data->mx, color_mt);

#ifdef FOR_AVISYNTH_26_ONLY
    const int bits_per_pixel = 8;
    const int pixelsize = 1;
    const int greyscale = avs_is_y8(&fi->vi);
#else
    const int bits_per_pixel = avs_bits_per_component(&fi->vi);
    const int pixelsize = avs_component_size(&fi->vi);
    const int greyscale = avs_is_y(&fi->vi);
#endif

    if (bits_per_pixel == 8)
      data->f_make_sub_img = make_sub_img;
    else if(bits_per_pixel <= 16)
      data->f_make_sub_img = make_sub_img16;
    else {
      v = avs_new_value_error("AssRender: unsupported bit depth: 32");
      avs_release_clip(c);
      return v;
    }


    switch (fi->vi.pixel_type)
    {
    case AVS_CS_YV12:
    case AVS_CS_I420:
        data->apply = apply_yv12;
        break;
    case AVS_CS_YUV420P10:
    case AVS_CS_YUV420P12:
    case AVS_CS_YUV420P14:
    case AVS_CS_YUV420P16:
        data->apply = apply_yuv420;
        break;
    case AVS_CS_YV16:
        data->apply = apply_yv16;
        break;
    case AVS_CS_YUV422P10:
    case AVS_CS_YUV422P12:
    case AVS_CS_YUV422P14:
    case AVS_CS_YUV422P16:
        data->apply = apply_yuv422;
        break;
    case AVS_CS_YV24:
    case AVS_CS_RGBP:
    case AVS_CS_RGBAP:
        data->apply = apply_yv24;
        break;
    case AVS_CS_YUV444P10:
    case AVS_CS_YUV444P12:
    case AVS_CS_YUV444P14:
    case AVS_CS_YUV444P16:
    case AVS_CS_RGBP10:
    case AVS_CS_RGBP12:
    case AVS_CS_RGBP14:
    case AVS_CS_RGBP16:
    case AVS_CS_RGBAP10:
    case AVS_CS_RGBAP12:
    case AVS_CS_RGBAP14:
    case AVS_CS_RGBAP16:
        data->apply = apply_yuv444;
        break;
    case AVS_CS_Y8:
        data->apply = apply_y8;
        break;
    case AVS_CS_Y10:
    case AVS_CS_Y12:
    case AVS_CS_Y14:
    case AVS_CS_Y16:
        data->apply = apply_y;
        break;
    case AVS_CS_YUY2:
        data->apply = apply_yuy2;
        break;
    case AVS_CS_BGR24:
        data->apply = apply_rgb;
        break;
    case AVS_CS_BGR32:
        data->apply = apply_rgba;
        break;
    case AVS_CS_BGR48:
        data->apply = apply_rgb48;
        break;
    case AVS_CS_BGR64:
        data->apply = apply_rgb64;
        break;
    case AVS_CS_YV411:
        data->apply = apply_yv411;
        break;
    default:
        v = avs_new_value_error("AssRender: unsupported pixel type");
        avs_release_clip(c);
        return v;
    }

    free(tmpcsp);

    const int buffersize = fi->vi.width * fi->vi.height * pixelsize;

    data->sub_img[0] = malloc(buffersize);
    data->sub_img[1] = malloc(buffersize);
    data->sub_img[2] = malloc(buffersize);
    data->sub_img[3] = malloc(buffersize);

    data->bits_per_pixel = bits_per_pixel;
    data->pixelsize = pixelsize;
    data->rgb_fullscale = avs_is_rgb(&fi->vi);
    data->greyscale = greyscale;

    fi->user_data = data;

    fi->get_frame = assrender_get_frame;

    v = avs_new_value_clip(c);
    avs_release_clip(c);

    avs_at_exit(env, assrender_destroy, data);

    return v;
}

const char* AVSC_CC avisynth_c_plugin_init(AVS_ScriptEnvironment* env)
{
    avs_add_function(env, "assrender",
                     "c[file]s[vfr]s[hinting]i[scale]f[line_spacing]f[dar]f"
                     "[sar]f[top]i[bottom]i[left]i[right]i[charset]s"
                     "[debuglevel]i[fontdir]s[srt_font]s[colorspace]s",
                     assrender_create, 0);
    return "AssRender: draws text subtitles better and faster than ever before";
}
void VS_CC assrender_destroy_vs(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    const VS_FilterInfo* d = instanceData;
    udata* ud = d->user_data;

    ass_renderer_done(((udata*)ud)->ass_renderer);
    ass_library_done(((udata*)ud)->ass_library);
    ass_free_track(((udata*)ud)->ass);

    free(((udata*)ud)->sub_img[0]);
    free(((udata*)ud)->sub_img[1]);
    free(((udata*)ud)->sub_img[2]);
    free(((udata*)ud)->sub_img[3]);

    if (((udata*)ud)->isvfr)
        free(((udata*)ud)->timestamp);

    free(ud);

    vsapi->freeNode(d->node);
    free(d);

}
void VS_CC assrender_init_vs(VSMap* in, VSMap* out, void** instanceData, VSNode* node, VSCore* core, const VSAPI* vsapi) {
    const VS_FilterInfo* d = *instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}
void VS_CC assrender_create_vs(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
    VS_FilterInfo* fi = malloc(sizeof(VS_FilterInfo));
    fi->node = vsapi->propGetNode(in, "clip", 0, NULL);
    fi->vi = vsapi->getVideoInfo(fi->node);
    char e[250];
    int err = 0;

    const char* f = vsapi->propGetData(in, "file", 0, &err);
    const char* vfr = vsapi->propGetData(in, "vfr", 0, &err);
    int h = vsapi->propGetInt(in, "hinting", 0, &err);
    double scale = vsapi->propGetFloat(in, "scale", 0, &err);
    if (err) scale = 1.0;
    double line_spacing = vsapi->propGetFloat(in, "line_spacing", 0, &err);
    double dar = vsapi->propGetFloat(in, "dar", 0, &err);
    double sar = vsapi->propGetFloat(in, "sar", 0, &err);
    int top = vsapi->propGetInt(in, "top", 0, &err);
    int bottom = vsapi->propGetInt(in, "bottom", 0, &err);
    int left = vsapi->propGetInt(in, "left", 0, &err);
    int right = vsapi->propGetInt(in, "right", 0, &err);
    const char* cs = vsapi->propGetData(in, "charset", 0, &err) ?
        vsapi->propGetData(in, "charset", 0, &err) : "UTF-8";
    int debuglevel = vsapi->propGetInt(in, "debuglevel", 0, &err);
    const char* fontdir = vsapi->propGetData(in, "fontdir", 0, &err) ?
#ifdef AVS_WINDOWS
        vsapi->propGetData(in, "fontdir", 0, &err) : "C:/Windows/Fonts";
#else
        vsapi->propGetData(in, "fontdir", 0, &err) : "/usr/share/fonts";
#endif
    const char* srt_font = vsapi->propGetData(in, "srt_font", 0, &err) ?
        vsapi->propGetData(in, "srt_font", 0, &err) : "sans-serif";
    const char* colorspace = vsapi->propGetData(in, "colorspace", 0, &err) ?
        vsapi->propGetData(in, "colorspace", 0, &err) : "";

    char* tmpcsp = calloc(1, BUFSIZ);
    strncpy(tmpcsp, colorspace, BUFSIZ - 1);

    ASS_Hinting hinting;
    udata* data;
    ASS_Track* ass;

    /*
    no unsupported colorspace left, bitness is checked at other place
    if (0 == 1) {
        v = avs_new_value_error(
                "AssRender: unsupported colorspace");
        avs_release_clip(c);
        return v;
    }
    */

    if (!f) {
        vsapi->setError(in, "AssRender: no input file specified");
        return;
    }

    switch (h) {
    case 0:
        hinting = ASS_HINTING_NONE;
        break;
    case 1:
        hinting = ASS_HINTING_LIGHT;
        break;
    case 2:
        hinting = ASS_HINTING_NORMAL;
        break;
    case 3:
        hinting = ASS_HINTING_NATIVE;
        break;
    default:
        vsapi->setError(out, "AssRender: invalid hinting mode");
        return;
    }

    data = malloc(sizeof(udata));

    if (!init_ass(fi->vi->width, fi->vi->height, scale, line_spacing, dar, sar,
        top, bottom, left, right, hinting, debuglevel, fontdir,
        data)) {
        vsapi->setError(out, "AssRender: failed to initialize");
        return;
    }

    if (!strcasecmp(strrchr(f, '.'), ".srt"))
        ass = parse_srt(f, data, srt_font);
    else {
        ass = ass_read_file(data->ass_library, (char*)f, (char*)cs);
        ass_read_matrix(f, tmpcsp);
    }

    if (!ass) {
        sprintf(e, "AssRender: unable to parse '%s'", f);
        vsapi->setError(out, e);
        return;
    }

    data->ass = ass;

    if (vfr) {
        int ver;
        FILE* fh = fopen(vfr, "r");

        if (!fh) {
            sprintf(e, "AssRender: could not read timecodes file '%s'", vfr);
            vsapi->setError(out, e);
            return;
        }

        data->isvfr = 1;

        if (fscanf(fh, "# timecode format v%d", &ver) != 1) {
            sprintf(e, "AssRender: invalid timecodes file '%s'", vfr);
            vsapi->setError(out, e);
            return;
        }

        switch (ver) {
        case 1:

            if (!parse_timecodesv1(fh, fi->vi->numFrames, data)) {
                vsapi->setError(out, "AssRender: error parsing timecodes file");
                return;
            }

            break;
        case 2:

            if (!parse_timecodesv2(fh, fi->vi->numFrames, data)) {
                vsapi->setError(out, "AssRender: timecodes file had less frames than expected");
                return;
            }

            break;
        }

        fclose(fh);
    }
    else {
        data->isvfr = 0;
    }

    matrix_type color_mt;

    if (fi->vi->format->colorFamily == cmRGB) {
        color_mt = MATRIX_NONE; // no RGB->YUV conversion
    }
    else {
        // .ASS "YCbCr Matrix" valid values are
        // "none" "tv.601" "pc.601" "tv.709" "pc.709" "tv.240m" "pc.240m" "tv.fcc" "pc.fcc"
        if (!strcasecmp(tmpcsp, "bt.709") || !strcasecmp(tmpcsp, "rec709") || !strcasecmp(tmpcsp, "tv.709")) {
            color_mt = MATRIX_BT709;
        }
        else if (!strcasecmp(tmpcsp, "pc.709")) {
            color_mt = MATRIX_PC709;
        }
        else if (!strcasecmp(tmpcsp, "bt.601") || !strcasecmp(tmpcsp, "rec601") || !strcasecmp(tmpcsp, "tv.601")) {
            color_mt = MATRIX_BT601;
        }
        else if (!strcasecmp(tmpcsp, "pc.601")) {
            color_mt = MATRIX_PC601;
        }
        else if (!strcasecmp(tmpcsp, "tv.fcc")) {
            color_mt = MATRIX_TVFCC;
        }
        else if (!strcasecmp(tmpcsp, "pc.fcc")) {
            color_mt = MATRIX_PCFCC;
        }
        else if (!strcasecmp(tmpcsp, "tv.240m")) {
            color_mt = MATRIX_TV240M;
        }
        else if (!strcasecmp(tmpcsp, "pc.240m")) {
            color_mt = MATRIX_PC240M;
        }
        else if (!strcasecmp(tmpcsp, "bt.2020") || !strcasecmp(tmpcsp, "rec2020")) {
            color_mt = MATRIX_BT2020;
        }
        else if (!strcasecmp(tmpcsp, "none") || !strcasecmp(tmpcsp, "guess")) {
            /* not yet
            * Theoretically only for 10 and 12 bits:
            if (fi->vi.width > 1920 || fi->vi.height > 1080)
              color_mt = MATRIX_BT2020;
            else
            */
            if (fi->vi->width > 1280 || fi->vi->height > 576)
                color_mt = MATRIX_PC709;
            else
                color_mt = MATRIX_PC601;
        }
        else {
            color_mt = MATRIX_BT601;
        }
    }

    FillMatrix(&data->mx, color_mt);

    const int bits_per_pixel = fi->vi->format->bitsPerSample;
    const int pixelsize = fi->vi->format->bytesPerSample;
    const int greyscale = fi->vi->format->colorFamily == cmGray;

    if (bits_per_pixel == 8)
        data->f_make_sub_img = make_sub_img;
    else if (bits_per_pixel <= 16)
        data->f_make_sub_img = make_sub_img16;
    else {
        vsapi->setError(out, "AssRender: unsupported bit depth: 32");
        return;
    }


    switch (fi->vi->format->id)
    {
    case pfYUV420P8:
        data->apply = apply_yv12;
        break;
    case pfYUV420P10:
    case pfYUV420P12:
    case pfYUV420P14:
    case pfYUV420P16:
        data->apply = apply_yuv420;
        break;
    case pfYUV422P8:
        data->apply = apply_yv16;
        break;
    case pfYUV422P10:
    case pfYUV422P12:
    case pfYUV422P14:
    case pfYUV422P16:
        data->apply = apply_yuv422;
        break;
    case pfYUV444P8:
        data->apply = apply_yv24;
        break;
    case pfYUV444P10:
    case pfYUV444P12:
    case pfYUV444P14:
    case pfYUV444P16:
        data->apply = apply_yuv444;
        break;
    case pfGray8:
        data->apply = apply_y8;
        break;
    case pfGray16:
        data->apply = apply_y;
        break;
    case pfRGB24:
        data->apply = apply_rgb;
        break;
    case pfRGB48:
        data->apply = apply_rgb48;
    default:
        vsapi->setError(out, "AssRender: unsupported pixel type");
        return;
    }

    free(tmpcsp);

    const int buffersize = fi->vi->width * fi->vi->height * pixelsize;

    data->sub_img[0] = malloc(buffersize);
    data->sub_img[1] = malloc(buffersize);
    data->sub_img[2] = malloc(buffersize);
    data->sub_img[3] = malloc(buffersize);

    data->bits_per_pixel = bits_per_pixel;
    data->pixelsize = pixelsize;
    data->rgb_fullscale = fi->vi->format->colorFamily == cmRGB;
    data->greyscale = greyscale;

    fi->user_data = data;

    vsapi->createFilter(in, out, userData, assrender_init_vs, assrender_get_frame_vs, assrender_destroy_vs, fmParallelRequests, 0, fi, core);

    return;
}
void VS_CC VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin) {
    configFunc("com.pinterf.assrender", "libass", "AssRender", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("TextSub",
        "clip:clip;"
        "file:data;"
        "vfr:data:opt;"
        "hinting:int:opt;"
        "scale:float:opt;"
        "line_spacing:float:opt;"
        "dar:float:opt;"
        "sar:float:opt;"
        "top:int:opt;"
        "bottom:int:opt;"
        "left:int:opt;"
        "right:int:opt;"
        "charset:data:opt;"
        "debuglevel:int:opt;"
        "fontdir:data:opt;"
        "srt_font:data:opt;"
        "colorspace:data:opt;",
        assrender_create_vs, "TextSub", plugin);
}