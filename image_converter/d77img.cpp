// D88/D77 Disk format
//	Header
//	Ofst		Size	Contents			Note
//	0x00		17		Disk name			(ended with '\0')
//	0x11		9		Reserved
//	0x1a		1		Write protect		0x00=No-protect	0x10=Protected
//	0x1b		1		Disk type			0x00:2D, 0x10:2DD, 0x20:2HD, 0x30:1D, 0x40:1DD
//	0x1c		4		Disk size			Head + all tracks
//	0x20		4*164	Track offset table	Offset to the track data from the top of the file
//
// Sector
//	Ofst		Size	Contents			Note
//	0x00		1		Cylinder#
//	0x01		1		Head#
//	0x02		1		Sector#
//	0x03		1		Secor size code		(128<<N)
//	0x04		2		# of sectors in the track
//	0x06		1		Density				0x00:Double density, 0x40:Single density, 0x01:High density
//	0x07		1		DAM					0x00:DAM, 0x10:DDAM
//	0x08		1		Status				0x00:Normal, 0x10:DDAM, 0xa0:ID-CRC, 0xb0:DT-CRC, 0xe0:No IDAM, 0xf0:No DAM
//	0x09		5		Reserved
//	0x0e		2		Sector data size
//	0x10		#		Sector data


#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <iomanip>

#include "d77img.h"

void d77img::read(const std::string& file_name) {
    std::ifstream ifs;
    m_image_size = 0;
    ifs.open("disk.d77", std::ios_base::binary);
    if (ifs.is_open() == false) {
        std::cout << "Failed to open file" << std::endl;
        return;
    }
    // Get image size
    ifs.seekg(0, std::ios_base::end);
    m_image_size = ifs.tellg();
    ifs.seekg(0, std::ios_base::beg);

    // Read image
    m_image_data.resize(m_image_size);
    ifs.read(reinterpret_cast<char*>(m_image_data.data()), m_image_size);

    // Parse image
    m_disk_name     = m_image_data.get_string_z(0);
    m_write_protect = m_image_data.get_byte_le(0x1a);
    m_disk_type     = m_image_data.get_byte_le(0x1b);
    m_disk_size     = m_image_data.get_dword_le(0x1c);

    m_disk_data.clear();
    m_disk_data.resize(164);

    for (size_t track = 0; track < 164; track++) {
        size_t track_offset = m_image_data.get_dword_le(0x20 + track * 4);
        if (track_offset != 0) {
            size_t num_sect = 0;
            do {
                sector_data sect;
                sect.m_C = m_image_data.get_byte_le(track_offset + 0);
                sect.m_H = m_image_data.get_byte_le(track_offset + 1);
                sect.m_R = m_image_data.get_byte_le(track_offset + 2);
                sect.m_N = m_image_data.get_byte_le(track_offset + 3);
                sect.m_num_sectors        = m_image_data.get_word_le(track_offset + 4);
                sect.m_density            = m_image_data.get_byte_le(track_offset + 6);
                sect.m_dam_type           = m_image_data.get_byte_le(track_offset + 7);
                sect.m_status             = m_image_data.get_byte_le(track_offset + 8);
                sect.m_sector_data_length = m_image_data.get_word_le(track_offset + 0x0e);
                sect.m_sector_data        = m_image_data.get_block(track_offset + 0x10, (128 << (sect.m_N & 3)));
                num_sect = sect.m_num_sectors;
                m_disk_data[track].push_back(sect);

                track_offset += 0x10 + sect.m_sector_data_length;

            } while (m_disk_data[track].size() < num_sect);
        }
    }
    m_image_data.clear();
    ifs.close();
}

void d77img::write(std::string file_name) {
    std::ofstream ofs;
    byte_array image;

    image.fill(0, 0x20 + 164 * 4, 0);				// Header + track offset table (for 164 tracks)
    image.set_string_z(0x00, m_disk_name, 17);
    image.set_byte_le(0x1a, m_write_protect);
    image.set_byte_le(0x1b, m_disk_type);
    size_t track_offset = 0x20 + 164 * 4;
    size_t num_track = media_max_track();

    for (size_t track_n = 0; track_n < num_track; track_n++) {
        size_t num_sect = m_disk_data[track_n].size();
        if (num_sect > 0) {
            image.set_dword_le(0x20 + track_n * 4, track_offset);			// track offset table

            for (size_t sect_n = 0; sect_n < num_sect; sect_n++) {
                sector_data sect = m_disk_data[track_n][sect_n];
                image.set_byte_le(track_offset + 0x00, sect.m_C);
                image.set_byte_le(track_offset + 0x01, sect.m_H);
                image.set_byte_le(track_offset + 0x02, sect.m_R);
                image.set_byte_le(track_offset + 0x03, sect.m_N);
                image.set_word_le(track_offset + 0x04, num_sect);

                image.set_byte_le(track_offset + 0x06, sect.m_density);
                image.set_byte_le(track_offset + 0x07, sect.m_dam_type);
                image.set_byte_le(track_offset + 0x08, sect.m_status);
                image.set_word_le(track_offset + 0x0e, sect.m_sector_data_length);
                for (size_t data_n = 0; data_n < sect.m_sector_data_length; data_n++) {
                    image.set_byte_le(track_offset + 0x10 + data_n, sect.m_sector_data[data_n]);
                }
                track_offset += 0x10 + sect.m_sector_data_length;
            }
        }
    }
    image.set_dword_le(0x1c, track_offset);

    ofs.open("disk_out.d77", std::ios_base::binary);
    if (ofs.is_open() == false) {
        std::cout << "Failed to open file" << std::endl;
        return;
    }
    ofs.write(reinterpret_cast<char*>(image.data()), image.size());
}

size_t d77img::media_max_track(void) {
    size_t num_track;
    switch (m_disk_type) {
    case 0x00:
        num_track = 84;
        break;
    case 0x10: // 2DD
    case 0x20: // 2HD
        num_track = 164;
        break;
    case 0x30: // 1D
        num_track = 44;
        break;
    case 0x40: // 1DD
        num_track = 84;
        break;
    default:
        num_track = 84;
        break;
    }
    return num_track;
}

byte_array d77img::read_sector(const size_t track, const size_t side, const size_t sect) {
    if (track * 2 + side < media_max_track()) {
        track_data& trk = m_disk_data[track * 2 + side];
        for (size_t sect_n = 0; sect_n < trk.size(); sect_n++) {
            if (trk[sect_n].m_C == track && trk[sect_n].m_R == sect) {
                return trk[sect_n].m_sector_data;
            }
        }
    }
    return byte_array();
}

bool d77img::write_sector(const size_t track, const size_t side, const size_t sect, const byte_array &sector_data) {
    if (track * 2 + side < media_max_track()) {
        track_data& trk = m_disk_data[track * 2 + side];
        for (size_t sect_n = 0; sect_n < trk.size(); sect_n++) {
            if (trk[sect_n].m_C == track && trk[sect_n].m_R == sect) {
                trk[sect_n].m_sector_data = sector_data;
                return true;
            }
        }
    }
    return false;
}
