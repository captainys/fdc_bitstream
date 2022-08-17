#define DLL_BODY
#include "dll_export.h"

#include "image_d77.h"
#include "d77img.h"
#include "byte_array.h"

#include "fdc_bitstream.h"

#include <iostream>

void disk_image_d77::read(const std::string file_name) {
    m_track_data_is_set = false;
    d77img input_image;
    input_image.read(file_name);
    m_base_prop.m_max_track_number = 0;
    m_base_prop.m_spindle_time_ns = (60*1e9)/300;
    m_base_prop.m_sampling_rate = 4e6;
    m_base_prop.m_data_bit_rate = 500e3;

    bit_array track_data;
    mfm_codec codec;
    fdc_crc crcgen;

    for(size_t track_n=0; track_n<input_image.m_disk_data.size(); track_n ++) {
        // Format a track by parsing D77 track data
        track_data.clear_array();
        track_data.set_array(m_base_prop.m_sampling_rate * (m_base_prop.m_spindle_time_ns / 1e9));
        codec.set_track_data(track_data);
        codec.set_pos(0);
        // preamble
        for(size_t i=0; i<80; i++) codec.mfm_write_byte(0x4e, false); // Gap4a
        for(size_t i=0; i<12; i++) codec.mfm_write_byte(0x00, false); // SYNC
        for(size_t i=0; i< 3; i++) codec.mfm_write_byte(0xf6, true);  // 0xc2
                                   codec.mfm_write_byte(0xfc, false); // IAM
        for(size_t i=0; i<50; i++) codec.mfm_write_byte(0x4e, false); // Gap1
        // sectors
        d77img::track_data image_track = input_image.m_disk_data[track_n]; 
        if (image_track.size() == 0) continue;
        for(size_t sect_n=0; sect_n<image_track.size(); sect_n++) {
            d77img::sector_data sect = image_track[sect_n];
            byte_array sect_body = sect.m_sector_data;
            for(size_t i=0; i<12; i++) codec.mfm_write_byte(0x00, false); // SYNC
            for(size_t i=0; i< 3; i++) codec.mfm_write_byte(0xf5, true);  // 0xa1
            crcgen.reset();
            codec.mfm_write_byte(0xfe, false); crcgen.data(0xfe); // IDAM
            codec.mfm_write_byte(sect.m_C, false); crcgen.data(sect.m_C);
            codec.mfm_write_byte(sect.m_H, false); crcgen.data(sect.m_H);
            codec.mfm_write_byte(sect.m_R, false); crcgen.data(sect.m_R);
            codec.mfm_write_byte(sect.m_N, false); crcgen.data(sect.m_N);
            uint16_t crcval;
            crcgen.data(0); crcgen.data(0);
            crcval = crcgen.get();
            codec.mfm_write_byte(crcval >> 8, false); codec.mfm_write_byte(crcval, false);   // CRC
            for(size_t i=0; i<22; i++) codec.mfm_write_byte(0x4e, false); // Gap2
            for(size_t i=0; i<12; i++) codec.mfm_write_byte(0x00, false); // SYNC
            for(size_t i=0; i< 3; i++) codec.mfm_write_byte(0xf5, true);  // 0xa1
            crcgen.reset();
            codec.mfm_write_byte(0xfb, false); crcgen.data(0xfb); // DAM
            for(size_t i=0; i<sect.m_sector_data_length; i++) {
                codec.mfm_write_byte(sect_body[i], false);
                crcgen.data(sect_body[i]);
            }
            crcgen.data(0); crcgen.data(0);
            crcval = crcgen.get();
            codec.mfm_write_byte(crcval >> 8, false); codec.mfm_write_byte(crcval, false);   // CRC
            for(size_t i=0; i<54; i++) codec.mfm_write_byte(0x4e, false); //Gap3
        }
        for(size_t i=0; i<152; i++) codec.mfm_write_byte(0x4e, false); //Gap4b

        track_data = codec.get_track_data();
        m_track_data[track_n] = track_data;
        m_base_prop.m_max_track_number = track_n + 1;
    }
    m_track_data_is_set = true;
}

void disk_image_d77::write(const std::string file_name) {
    fdc_bitstream fdc;
    d77img output_image;

    output_image.m_disk_name = "D77IMG";
    output_image.m_write_protect = 0;
    output_image.m_disk_type = 0;       // 2D
    output_image.m_disk_size = 0;

    for (size_t track_n = 0; track_n < m_base_prop.m_max_track_number; track_n++) {
        d77img::track_data d77_trk;
        bit_array mfm_trk = m_track_data[track_n];
        fdc.set_track_data(mfm_trk);
        fdc.set_pos(0);
        std::vector<fdc_bitstream::id_field> id_list = fdc.read_all_idam();

        for (size_t sect_n = 0; sect_n < id_list.size(); sect_n++) {
            d77img::sector_data sect_dt;
            sect_dt.m_C = id_list[sect_n].C;
            sect_dt.m_H = id_list[sect_n].H;
            sect_dt.m_R = id_list[sect_n].R;
            sect_dt.m_N = id_list[sect_n].N;
            sect_dt.m_dam_type = 0;
            sect_dt.m_density = 0;
            sect_dt.m_num_sectors = id_list.size();
            fdc_bitstream::sector_data read_sect = fdc.read_sector(sect_dt.m_C, sect_dt.m_H, sect_dt.m_R);
            sect_dt.m_sector_data = read_sect.data;
            sect_dt.m_sector_data_length = read_sect.data.size();
            d77_trk.push_back(sect_dt);
        }
        output_image.m_disk_data.push_back(d77_trk);
    }
    output_image.write(file_name);
}