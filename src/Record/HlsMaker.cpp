/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "HlsMaker.h"
namespace mediakit {

HlsMaker::HlsMaker(float seg_duration, uint32_t seg_number) {
    //最小允许设置为0，0个切片代表点播
    _seg_number = seg_number;
    _seg_duration = seg_duration;
}

HlsMaker::~HlsMaker() {
}

void HlsMaker::updateSegIndex() {
    // 片内时间戳，todo: 需要考虑中断重启后的影响
    uint32_t seg_timestamp = _last_idr_timestamp - _last_seg_timestamp;
    // 计算GOP时长
    uint32_t gop_dur = _last_timestamp - _last_idr_timestamp;
    _last_idr_timestamp = _last_timestamp;
    if (_last_file_name.empty()) {
        // 如果没有切片文件在，则不需要记录索引，一般发生在第一个idr帧出现时
        return;
    }

    string index = StrPrinter << seg_timestamp << ',' << gop_dur << ',' << _last_idr_pos << ',' << _last_data_len << ',' << _last_data_len - _last_idr_pos << '\n';
    _last_idr_pos = _last_data_len;
    InfoL << "index added:" << index;
    onWriteSegIndex(index.data(), index.size());
}

void HlsMaker::inputData(void *data, uint32_t len, uint32_t timestamp, bool is_idr_fast_packet) {
    if (data && len) {
        _last_timestamp = timestamp;
        if (is_idr_fast_packet) {
            updateSegIndex();
            //尝试切片ts
            addNewSegment(timestamp);
        }
        if (!_last_file_name.empty()) {
            //存在切片才写入ts数据
            onWriteSegment((char *)data, len);
            _last_data_len += len;
        }
    } else {
        //resetTracks时触发此逻辑
        flushLastSegment(true);
    }
}

void HlsMaker::addNewSegment(uint32_t stamp) {
    auto strHour = getTimeStr("%H");
    if (strHour == _current_hour) {  // 每小时一个变换，当前小时没变化，记录在同一个文件中
        return;
    }
    _current_hour = strHour;

    //关闭并保存上一个切片
    flushLastSegment(false);
    //新增切片
    _last_file_name = onOpenSegment(0);
    //记录本次切片的起始时间戳
    _last_seg_timestamp = stamp;
    _last_idr_pos = 0;
    //准备记录数据长度
    _last_data_len = 0;
}

void HlsMaker::flushLastSegment(bool eof) {
    if (_last_file_name.empty()) {
        //不存在上个切片
        return;
    }

    //todo: 更新全局索引文件
}

bool HlsMaker::isLive() {
    return _seg_number != 0;
}

void HlsMaker::clear() {
    _file_index = 0;
    _last_seg_timestamp = 0;
    _seg_dur_list.clear();
    _last_file_name.clear();
}

}  //namespace mediakit