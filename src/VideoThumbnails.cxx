// SPDX-License-Identifier: GPL-2.0-or-later
#include "VideoThumbnails.H"
#include "VideoFileGmerlin.H"
#include "LazyFrame.H"
#include "DiskCache.H"
#include <FL/Fl_Shared_Image.H>
#include <cstring>
namespace nle {
bool VideoThumbnails::get(const char *filename,unsigned char *rgb,int &w,int &h) {
  DiskCache cache(filename,"ffmpeg-thumb-v1");
  if(!cache.isEmpty() && cache.size()>=int(2*sizeof(int)+VIDEO_THUMBNAIL_WIDTH*VIDEO_THUMBNAIL_HEIGHT*3)) {
    cache.read(&w,sizeof(w));cache.read(&h,sizeof(h));cache.read(rgb,VIDEO_THUMBNAIL_WIDTH*VIDEO_THUMBNAIL_HEIGHT*3);return true;
  }
  VideoFileGmerlin video(filename);
  if(video.ok()) {
    auto frame=video.getFrame(0);if(!frame)return false;
    w=video.width();h=video.height();
    frame->set_rgb_target(VIDEO_THUMBNAIL_WIDTH,VIDEO_THUMBNAIL_HEIGHT);
    auto target=frame->target();
    for(int y=0;y<VIDEO_THUMBNAIL_HEIGHT;++y)std::memcpy(rgb+y*VIDEO_THUMBNAIL_WIDTH*3,target->planes[0]+y*target->strides[0],VIDEO_THUMBNAIL_WIDTH*3);
  } else {
    auto image=Fl_Shared_Image::get(filename);if(!image)return false;
    w=image->w();h=image->h();
    auto scaled=image->copy(VIDEO_THUMBNAIL_WIDTH,VIDEO_THUMBNAIL_HEIGHT);image->release();
    if(!scaled || (scaled->d()!=3 && scaled->d()!=4)){delete scaled;return false;}
    const auto data=reinterpret_cast<const unsigned char*>(scaled->data()[0]);
    const int stride=scaled->ld()?scaled->ld():scaled->w()*scaled->d();
    for(int y=0;y<VIDEO_THUMBNAIL_HEIGHT;++y)for(int x=0;x<VIDEO_THUMBNAIL_WIDTH;++x)
      std::memcpy(rgb+(y*VIDEO_THUMBNAIL_WIDTH+x)*3,data+y*stride+x*scaled->d(),3);
    delete scaled;
  }
  cache.write(&w,sizeof(w));cache.write(&h,sizeof(h));cache.write(rgb,VIDEO_THUMBNAIL_WIDTH*VIDEO_THUMBNAIL_HEIGHT*3);cache.clean();return true;
}
}
