#pragma once

#include "image_raw.h"

void disk_image_raw::read(std::string raw_file_name) {
    std::ifstream ifs(raw_file_name);
    if (ifs.is_open() == false) {
        std::cerr << "Failed to open " << raw_file_name << "." << std::endl;
        return;
    }
    std::string  buf;
    bool read_track_mode = false;
    size_t cylinder = 0, side = 0;
    bit_array track_data;
    size_t bit_pos = 0;
    while (std::getline(ifs, buf))
    {
        if (read_track_mode && buf[0] == '~') {         // decode bit stream data
            for (size_t i = 1; i < buf.size(); i++)
            {
                size_t val = buf[i] - ' ';
                bit_pos += val;
                track_data.set(bit_pos, 1);
            }
        }
        else {
            std::vector<std::string> items;
            std::stringstream ss{ buf };
            std::string tmpbuf;
            while (std::getline(ss, tmpbuf, ' '))
            {
                items.push_back(tmpbuf);
            }
            if (items.size() >= 1) {
                if (items[0] == "**TRACK_RANGE") {
                }
                else if (items[0] == "**TRACK_READ") {
                    cylinder = atoi(items[1].c_str());
                    side = atoi(items[2].c_str());
                    track_data.clear_array();
                    bit_pos = 0;
                    read_track_mode = true;
                }
                else if (items[0] == "**TRACK_END") {
                    m_track_data[cylinder * 2 + side] = track_data;
                    read_track_mode = false;
                }
                else if (items[0] == "**MEDIA_TYPE") {
                }
                else if (items[0] == "**START") {
                }
                else if (items[0] == "**DRIVE_TYPE") {
                }
                else if (items[0] == "**SPIN_SPD") {
                }
                else if (items[0] == "**OVERLAP") {
                }
            }
        }
    }
}
