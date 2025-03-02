﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "HlsMakerImp.h"

#include <sys/stat.h>

#include <ctime>

#include "Util/util.h"
#include "Util/uv_errno.h"

using namespace toolkit;

namespace mediakit {

HlsMakerImp::HlsMakerImp(const string &m3u8_file,
                         const string &params,
                         uint32_t bufSize,
                         float seg_duration,
                         uint32_t seg_number) : HlsMaker(seg_duration, seg_number) {
    _path_prefix = m3u8_file.substr(0, m3u8_file.rfind('/'));
    _path_hls = m3u8_file;
    _params = params;
    _buf_size = bufSize;
    _file_buf.reset(new char[bufSize], [](char *ptr) {
        delete[] ptr;
    });

    _info.folder = _path_prefix;
}

HlsMakerImp::~HlsMakerImp() {
    clearCache();
}

void HlsMakerImp::clearCache() {
    //录制完了
    flushLastSegment(true);
}

string HlsMakerImp::onOpenSegment(int index) {
    string segment_name, segment_path, segment_index_path;
    {
        auto strDate = getTimeStr("%Y-%m-%d");
        auto strHour = getTimeStr("%H-%M-%S");
        segment_name = StrPrinter << strDate + "/" + strHour << ".ts";
        segment_path = _path_prefix + "/" + segment_name;
        segment_index_path = segment_path + ".idx";
    }
    _file = makeFile(segment_path, true);
    InfoL << "recording segment file created:" << segment_path;
    _seg_index_file = makeFile(segment_index_path, false);
    InfoL << "recording segment index created:" << segment_index_path;

    //保存本切片的元数据
    _info.start_time = ::time(NULL);
    _info.file_name = segment_name;
    _info.file_path = segment_path;
    _info.url = _info.app + "/" + _info.stream + "/" + segment_name;

    if (!_file) {
        WarnL << "create file failed," << segment_path << " " << get_uv_errmsg();
    }

    if (!_seg_index_file) {
        WarnL << "create file failed," << segment_index_path << " " << get_uv_errmsg();
    }

    if (_params.empty()) {
        return segment_name;
    }
    return segment_name + "?" + _params;
}

void HlsMakerImp::onDelSegment(int index) {
    auto it = _segment_file_paths.find(index);
    if (it == _segment_file_paths.end()) {
        return;
    }
    File::delete_file(it->second.data());
    _segment_file_paths.erase(it);
}

void HlsMakerImp::onWriteSegment(const char *data, int len) {
    if (_file) {
        fwrite(data, len, 1, _file.get());
    }
    if (_media_src) {
        _media_src->onSegmentSize(len);
    }
}

void HlsMakerImp::onWriteSegIndex(const char *data, int len) {
    if (_seg_index_file) {
        fwrite(data, len, 1, _seg_index_file.get());
        fflush(_seg_index_file.get());
    } else {
        WarnL << "write segment index file failed"
              << " " << get_uv_errmsg();
    }
}

void HlsMakerImp::onWriteHls(const char *data, int len) {
    auto hls = makeFile(_path_hls);
    if (hls) {
        fwrite(data, len, 1, hls.get());
        hls.reset();
        if (_media_src) {
            _media_src->registHls(true);
        }
    } else {
        WarnL << "create hls file failed," << _path_hls << " " << get_uv_errmsg();
    }
    //DebugL << "\r\n"  << string(data,len);
}

void HlsMakerImp::onFlushLastSegment(uint32_t duration_ms) {
    GET_CONFIG(bool, broadcastRecordTs, Hls::kBroadcastRecordTs);
    if (broadcastRecordTs) {
        //关闭ts文件以便获取正确的文件大小
        _file = nullptr;
        _info.time_len = duration_ms / 1000.0;
        struct stat fileData;
        stat(_info.file_path.data(), &fileData);
        _info.file_size = fileData.st_size;
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastRecordTs, _info);
    }
}

std::shared_ptr<FILE> HlsMakerImp::makeFile(const string &file, bool setbuf) {
    auto file_buf = _file_buf;
    auto ret = shared_ptr<FILE>(File::create_file(file.data(), "wb"), [file_buf](FILE *fp) {
        if (fp) {
            fclose(fp);
        }
    });
    if (ret && setbuf) {
        setvbuf(ret.get(), _file_buf.get(), _IOFBF, _buf_size);
    }
    return ret;
}

void HlsMakerImp::setMediaSource(const string &vhost, const string &app, const string &stream_id) {
    _media_src = std::make_shared<HlsMediaSource>(vhost, app, stream_id);
    _info.app = app;
    _info.stream = stream_id;
    _info.vhost = vhost;
}

HlsMediaSource::Ptr HlsMakerImp::getMediaSource() const {
    return _media_src;
}

}  //namespace mediakit