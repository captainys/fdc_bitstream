/**
 * @file image_hfe.cpp
 * @author Yasunori Shimura (yasu0710@gmail.com)
 * @brief `HFE` floppy image reader (HxC floppy emulator format)
 * @version 
 * @date 2022-08-12
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include "image_hfe.h"

void flip_bit_order(std::vector<uint8_t> &vec) {
	for(auto it = vec.begin(); it != vec.end(); it++) {
		uint8_t src = *it;
		uint8_t tmp = 0;
		for(int i=0; i<8; i++) {
			tmp = (tmp << 1) | (src & 1);
			src >>= 1;
		}
		*it = tmp;
	}
}

void disk_image_hfe::read(const std::string file_name) {
	m_track_data_is_set = false;
	hfe_header header;
	std::ifstream ifs = open_binary_file(file_name);
	ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (memcmp(header.HEADERSIGNATURE, "HXCPICFE", 8) != 0) {								// Header signature mismatch
		return;
	}
	if (header.number_of_track > 84) {
		header.number_of_track = 84;
	}
	m_base_prop.m_number_of_tracks = header.number_of_track * header.number_of_side;		// HFE(MFM,2D) = 42, 2
	if (header.floppyRPM == 0) {
		m_base_prop.m_spindle_time_ns = 0.2 * 1e9;
	}
	else {
		m_base_prop.m_spindle_time_ns = (60 * 1e9) / header.floppyRPM;
	}
	m_base_prop.m_data_bit_rate = header.bitRate * 2e3;			// HFE(MFM,2D) == 250??
	m_base_prop.m_sampling_rate = 4e6;

	hfe_track track_offset_table[84];
	ifs.seekg(header.track_list_offset * 0x0200, std::ios_base::beg);
	ifs.read(reinterpret_cast<char*>(track_offset_table), header.number_of_track * sizeof(hfe_track));

	for (size_t track = 0; track < header.number_of_track; track++) {
		bit_array side0;
		bit_array side1;
		side0.resize(4e6 * 0.2);		// reserve capacity in advance to speed up
		side1.resize(4e6 * 0.2);
		side0.set_stream_pos(0);
		side1.set_stream_pos(0);
		std::vector<uint8_t> buf;
		size_t blocks = track_offset_table[track].track_len / 0x0200;
		size_t fraction = track_offset_table[track].track_len % 0x0200;
		size_t read_blocks = blocks + (fraction > 0 ? 1 : 0);
		size_t bit_cell_width = m_base_prop.m_sampling_rate / m_base_prop.m_data_bit_rate;
		buf.resize(read_blocks * 0x0200);
		ifs.seekg(track_offset_table[track].offset * 0x0200, std::ios_base::beg);
		ifs.read(reinterpret_cast<char*>(buf.data()), read_blocks * 0x0200);
		size_t blk_id;
		for (blk_id = 0; blk_id < blocks; blk_id++) {
			for (size_t ofst = 0; ofst < 0x0100; ofst++) {
				for (uint16_t bit_pos = 0x01; bit_pos < 0x100; bit_pos <<= 1) {
					int bit_data;
					bit_data = (buf[blk_id * 0x0200 + ofst] & bit_pos) ? 1 : 0;
					for (size_t j = 0; j < bit_cell_width; j++) {
						if (j == bit_cell_width / 2) side0.write_stream(bit_data, true);
						else                         side0.write_stream(0, true);
					}
					bit_data = (buf[blk_id * 0x0200 + ofst + 0x0100] & bit_pos) ? 1 : 0;
					for (size_t j = 0; j < bit_cell_width; j++) {
						if (j == bit_cell_width / 2) side1.write_stream(bit_data, true);
						else                         side1.write_stream(0, true);
					}
				}
			}
		}
		if (fraction > 0) {
			for (size_t ofst = 0; ofst < fraction / 2; ofst++) {
				for (uint16_t bit_pos = 0x01; bit_pos < 0x100; bit_pos <<= 1) {
					int bit_data;
					bit_data = (buf[blk_id * 0x0200 + ofst] & bit_pos) ? 1 : 0;
					for (size_t j = 0; j < bit_cell_width; j++) {
						if (j == bit_cell_width / 2) side0.write_stream(bit_data, true);
						else                         side0.write_stream(0, true);
					}

					bit_data = (buf[blk_id * 0x0200 + ofst + 0x0100] & bit_pos) ? 1 : 0;
					for (size_t j = 0; j < bit_cell_width; j++) {
						if (j == bit_cell_width / 2) side1.write_stream(bit_data, true);
						else                         side1.write_stream(0, true);
					}
				}
			}
		}
		m_track_data[track * 2 + 0] = side0;
		m_track_data[track * 2 + 1] = side1;
	}
	m_track_data_is_set = true;
}

void disk_image_hfe::write(const std::string file_name) {
	if(m_track_data_is_set == false) return;

	hfe_header header;
	memset(&header, 0, sizeof(hfe_header));
    std::ofstream ofs;
    ofs.open(file_name, std::ios::out | std::ios::binary);
	memcpy(header.HEADERSIGNATURE, "HXCPICFE", 8);
	header.formatrevision = 0;
	header.number_of_track = m_base_prop.m_number_of_tracks / 2;
	header.number_of_side = 2;
	header.track_encoding = floppyinterfacemode_t::IBMPC_DD_FLOPPYMODE;
	header.bitRate = m_base_prop.m_data_bit_rate / 2e3;		// MFM=250 (==250KHz)
	header.floppyRPM = 0; //60e9/m_base_prop.m_spindle_time_ns;
	header.floppyinterfacemode = floppyinterfacemode_t::GENERIC_SHUGGART_DD_FLOPPYMODE;
	header.dnu = 1;  // free (but 1 is set in the .hfe file generated by the HxC tool)
	header.track_list_offset = 1;			// 1 = 0x200 (multiplied by 0x200)
	header.write_allowed = 0xff;
	header.single_step   = 0xff;			// 0xff: single step, 0x00: double step
	header.track0s0_altencoding = 0xff;
	header.track0s0_encoding    = 0xff;
	header.track0s1_altencoding = 0xff;
	header.track0s1_encoding    = 0xff;
	ofs.seekp(0);
	ofs.write(reinterpret_cast<char*>(&header), sizeof(hfe_header));

	hfe_track track_table[80];
	memset(track_table, 0, sizeof(hfe_track));

	std::vector<uint8_t> side0, side1;

	size_t track_data_ofst = 0x400;

	for(size_t cylinder_n = 0; cylinder_n < m_base_prop.m_number_of_tracks / 2; cylinder_n++) {
		size_t track_n = cylinder_n * 2;
		side0 = simple_raw_to_mfm(m_track_data[track_n    ]).get_array();
		side1 = simple_raw_to_mfm(m_track_data[track_n + 1]).get_array();
		flip_bit_order(side0);
		flip_bit_order(side1);
		size_t mpx_track_size = side0.size() + side1.size();
		char* mpx_track = new char[align(mpx_track_size, 0x400)];
		memset(mpx_track, 0, mpx_track_size);

		size_t mpx_ptr  = 0;
		size_t dst_ofst = 0;
		size_t size0 = 0, size1 = 0;
		while(mpx_ptr < side0.size() || mpx_ptr < side1.size()) {
			if(mpx_ptr < side0.size()) {
				size0 = (side0.size() - mpx_ptr >= 0x100) ? 0x100 : side0.size() - mpx_ptr;
				memcpy(mpx_track + dst_ofst, side0.data() + mpx_ptr, size0);
			}
			dst_ofst += 0x100;
			if(mpx_ptr < side1.size()) {
				size1 = (side1.size() - mpx_ptr >= 0x100) ? 0x100 : side1.size() - mpx_ptr;
				memcpy(mpx_track + dst_ofst, side1.data() + mpx_ptr, size1);
			}
			dst_ofst += 0x100;
			mpx_ptr  += 0x100;
		}
		ofs.seekp(track_data_ofst);
		ofs.write(mpx_track, dst_ofst);
		track_table[cylinder_n].offset = track_data_ofst / 0x200;
		track_table[cylinder_n].track_len = mpx_track_size;
		//track_table[cylinder_n].track_len = (size0 > size1) ? size0 : size1;
		track_data_ofst = align(track_data_ofst + mpx_track_size, 0x200);
		delete[] mpx_track;
	}
	// write track offset table
	ofs.seekp(0x200);
	ofs.write(reinterpret_cast<char*>(track_table), sizeof(hfe_track) * 80);
	ofs.close();
}
