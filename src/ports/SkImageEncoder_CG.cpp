/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTypes.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)

#include "SkBitmap.h"
#include "SkCGUtils.h"
#include "SkColorPriv.h"
#include "SkData.h"
#include "SkImageEncoderPriv.h"
#include "SkStream.h"
#include "SkStreamPriv.h"
#include "SkTemplates.h"
#include "SkUnPreMultiply.h"

#ifdef SK_BUILD_FOR_MAC
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef SK_BUILD_FOR_IOS
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <MobileCoreServices/MobileCoreServices.h>
#endif

static size_t consumer_put(void* info, const void* buffer, size_t count) {
    SkWStream* stream = reinterpret_cast<SkWStream*>(info);
    return stream->write(buffer, count) ? count : 0;
}

static void consumer_release(void* info) {
    // we do nothing, since by design we don't "own" the stream (i.e. info)
}

static CGDataConsumerRef SkStreamToCGDataConsumer(SkWStream* stream) {
    CGDataConsumerCallbacks procs;
    procs.putBytes = consumer_put;
    procs.releaseConsumer = consumer_release;
    // we don't own/reference the stream, so it our consumer must not live
    // longer that our caller's ownership of the stream
    return CGDataConsumerCreate(stream, &procs);
}

static CGImageDestinationRef SkStreamToImageDestination(SkWStream* stream,
                                                        CFStringRef type) {
    CGDataConsumerRef consumer = SkStreamToCGDataConsumer(stream);
    if (nullptr == consumer) {
        return nullptr;
    }
    SkAutoTCallVProc<const void, CFRelease> arconsumer(consumer);

    return CGImageDestinationCreateWithDataConsumer(consumer, type, 1, nullptr);
}

class SkImageEncoder_CG : public SkImageEncoder {
public:
    SkImageEncoder_CG(SkEncodedImageFormat t) : fType(t) {}

protected:
    virtual bool onEncode(SkWStream* stream, const SkBitmap& bm, int quality);

private:
    SkEncodedImageFormat fType;
};

/*  Encode bitmaps via CGImageDestination. We setup a DataConsumer which writes
    to our SkWStream. Since we don't reference/own the SkWStream, our consumer
    must only live for the duration of the onEncode() method.
 */
bool SkImageEncoder_CG::onEncode(SkWStream* stream, const SkBitmap& bm,
                                 int quality) {
    // Used for converting a bitmap to 8888.
    const SkBitmap* bmPtr = &bm;
    SkBitmap bitmap8888;

    CFStringRef type;
    switch (fType) {
        case SkEncodedImageFormat::kICO:
            type = kUTTypeICO;
            break;
        case SkEncodedImageFormat::kBMP:
            type = kUTTypeBMP;
            break;
        case SkEncodedImageFormat::kGIF:
            type = kUTTypeGIF;
            break;
        case SkEncodedImageFormat::kJPEG:
            type = kUTTypeJPEG;
            break;
        case SkEncodedImageFormat::kPNG:
            // PNG encoding an ARGB_4444 bitmap gives the following errors in GM:
            // <Error>: CGImageDestinationAddImage image could not be converted to destination
            // format.
            // <Error>: CGImageDestinationFinalize image destination does not have enough images
            // So instead we copy to 8888.
            if (bm.colorType() == kARGB_4444_SkColorType) {
                bm.copyTo(&bitmap8888, kN32_SkColorType);
                bmPtr = &bitmap8888;
            }
            type = kUTTypePNG;
            break;
        default:
            return false;
    }

    CGImageDestinationRef dst = SkStreamToImageDestination(stream, type);
    if (nullptr == dst) {
        return false;
    }
    SkAutoTCallVProc<const void, CFRelease> ardst(dst);

    CGImageRef image = SkCreateCGImageRef(*bmPtr);
    if (nullptr == image) {
        return false;
    }
    SkAutoTCallVProc<CGImage, CGImageRelease> agimage(image);

    CGImageDestinationAddImage(dst, image, nullptr);
    return CGImageDestinationFinalize(dst);
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_USE_CG_ENCODER
static SkImageEncoder* sk_imageencoder_cg_factory(SkEncodedImageFormat t) {
    switch (t) {
        case SkEncodedImageFormat::kICO:
        case SkEncodedImageFormat::kBMP:
        case SkEncodedImageFormat::kGIF:
        case SkEncodedImageFormat::kJPEG:
        case SkEncodedImageFormat::kPNG:
            break;
        default:
            return nullptr;
    }
    return new SkImageEncoder_CG(t);
}

static SkImageEncoder_EncodeReg gEReg(sk_imageencoder_cg_factory);
#endif

SkImageEncoder* CreateImageEncoder_CG(SkImageEncoder::Type type) {
    return new SkImageEncoder_CG((SkEncodedImageFormat)type);
}

#endif//defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
