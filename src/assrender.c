#include "assrender.h"
#include "render.h"
#include "sub.h"
#include "timecodes.h"

static char* read_file_bytes(FILE* fp, size_t* bufsize)
{
    int res;
    long sz;
    long bytes_read;
    char* buf;
    res = fseek(fp, 0, SEEK_END);
    if (res == -1) {
        fclose(fp);
        return 0;
    }

    sz = ftell(fp);
    rewind(fp);

    buf = sz < SIZE_MAX ? malloc(sz + 1) : NULL;
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    assert(buf);
    bytes_read = 0;
    do {
        res = fread(buf + bytes_read, 1, sz - bytes_read, fp);
        if (res <= 0) {
            fclose(fp);
            free(buf);
            return 0;
        }
        bytes_read += res;
    } while (sz - bytes_read > 0);
    buf[sz] = '\0';
    fclose(fp);

    if (bufsize)
        *bufsize = sz;
    return buf;
}

static const char* detect_bom(const char* buf, const size_t bufsize) {
    if (bufsize >= 4) {
        if (!strncmp(buf, "\xef\xbb\xbf", 3))
            return "UTF-8";
        if (!strncmp(buf, "\x00\x00\xfe\xff", 4))
            return "UTF-32BE";
        if (!strncmp(buf, "\xff\xfe\x00\x00", 4))
            return "UTF-32LE";
        if (!strncmp(buf, "\xfe\xff", 2))
            return "UTF-16BE";
        if (!strncmp(buf, "\xff\xfe", 2))
            return "UTF-16LE";
    }
    return "UTF-8";
}

#if defined(_MSC_VER)
#include <iconv.h>
static wchar_t* utf8_to_utf16le(const char* data, size_t size) {
    iconv_t icdsc;
    char* outbuf;

    if ((icdsc = iconv_open("UTF-16LE", "UTF-8")) == (iconv_t)(-1)) {
        return NULL;
    }

    {
        size_t osize = size;
        size_t ileft = size;
        size_t oleft = size - 1;
        char* ip;
        char* op;
        size_t rc;
        int clear = 0;

        outbuf = malloc(osize);
        if (!outbuf)
            goto out;
        ip = data;
        op = outbuf;

        for(;;) {
            if (ileft)
                rc = iconv(icdsc, &ip, &ileft, &op, &oleft);
            else {              // clear the conversion state and leave
                clear = 1;
                rc = iconv(icdsc, NULL, NULL, &op, &oleft);
            }
            if (rc == (size_t)(-1)) {
                if (errno == E2BIG) {
                    size_t offset = op - outbuf;
                    char* nbuf = realloc(outbuf, osize + size);
                    if (!nbuf) {
                        free(outbuf);
                        outbuf = NULL;
                        goto out;
                    }
                    outbuf = nbuf;
                    op = outbuf + offset;
                    osize += size;
                    oleft += size;
                }
                else {
                    free(outbuf);
                    outbuf = NULL;
                    goto out;
                }
            }
            else if (clear)
                break;
        }
        outbuf[osize - oleft - 1] = outbuf[osize - oleft] = 0;
    }

out:
    if (icdsc != (iconv_t)(-1)) {
        (void)iconv_close(icdsc);
    }

    return outbuf;
}
#endif

static FILE* open_utf8_filename(const char* f, const char* m)
{
#if defined(_MSC_VER)
    wchar_t* file_name = utf8_to_utf16le(f, strlen(f));
    wchar_t* mode = utf8_to_utf16le(m, strlen(m));
    FILE* fp = _wfopen(file_name, mode);
    free(file_name);
    free(mode);
    return fp;
#else
    return fopen(f, m);
#endif
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
    const char* cs = vsapi->propGetData(in, "charset", 0, &err);
    if (err) cs = NULL;
    int debuglevel = vsapi->propGetInt(in, "debuglevel", 0, &err);
    const char* fontdir = vsapi->propGetData(in, "fontdir", 0, &err);
    if (err) 
#ifdef _WINDOWS
        fontdir = "";
#else
        fontdir = "/usr/share/fonts";
#endif
    const char* srt_font = vsapi->propGetData(in, "srt_font", 0, &err);
    if (err) srt_font = "sans-serif";
    const char* colorspace = vsapi->propGetData(in, "colorspace", 0, &err);
    if (err) colorspace = "";

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

    if (!strcasecmp(strrchr(f, '.'), ".srt")) {
        FILE* fp = open_utf8_filename(f, "r");
        ass = parse_srt(fp, data, srt_font);
    }
    else {
        FILE* fp = open_utf8_filename(f, "rb");
        size_t bufsize;
        char* buf = read_file_bytes(fp, &bufsize);
        if (cs == NULL) cs = detect_bom(buf, bufsize);
        ass = ass_read_memory(data->ass_library, buf, bufsize, (char*)cs);
        fp = open_utf8_filename(f, "r");
        ass_read_matrix(fp, tmpcsp);
    }

    if (!ass) {
        sprintf(e, "AssRender: unable to parse '%s'", f);
        vsapi->setError(out, e);
        return;
    }

    data->ass = ass;

    if (vfr) {
        int ver;
        FILE* fh = open_utf8_filename(vfr, "r");

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
    case pfRGB24:
        data->apply = apply_yv24;
        break;
    case pfYUV444P10:
    case pfYUV444P12:
    case pfYUV444P14:
    case pfYUV444P16:
    case pfRGB48:
        data->apply = apply_yuv444;
        break;
    case pfGray8:
        data->apply = apply_y8;
        break;
    case pfGray16:
        data->apply = apply_y;
        break;
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
    configFunc("com.pinterf.assrender", "assrender", "AssRender", VAPOURSYNTH_API_VERSION, 1, plugin);
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