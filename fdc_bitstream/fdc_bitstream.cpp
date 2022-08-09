#pragma once

/**
* @file fdc_bitstream.cpp
* @brief MFM Floppy FDC emulator
* @author Yasunori Shimura
* 
* @details A FDC emulator that handles 'raw' (uncooked) MFM/IBM-PC format floppy disk bit stream data as a track data.
*/

#include <vector>

#include "fdc_bitstream.h"
//#include "fdc_crc.h"
//#include "mfm_codec.h"

//#define DEBUG

/**
* Read track.
* Reads track data from the current position until the pointer reaches to the end of the track (no wrap around).
* 
* @param[in] none
* @return std::vector<uint8_t> Read track data (Decoded MFM data)
*/
std::vector<uint8_t> fdc_bitstream::read_track(void) {
    uint8_t read_data;
    bool missing_clock;
    std::vector<uint8_t> track_data;
    m_codec.clear_wraparound();
    while (m_codec.is_wraparound() == false) {
        m_codec.mfm_read_byte(read_data, missing_clock, false, false);
#ifdef DEBUG
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(read_data) << " ";
#endif
        track_data.push_back(read_data);
    }
    return track_data;
}

/**
* Write track.  
* Start writing from the current position until all data is written. 
* The track buffer is treated as a ring buffer, so the pointer will wrap around if the write data goes over the buffer's end.
* Write track recognizes MB8876/FDC179x compatible special codes such as $f5, $f6, $f7, $fb, $fe, and so on.
* 
* @param[in] track_buf Write track data
* @return none
*/
void fdc_bitstream::write_track(const std::vector<uint8_t>& track_buf) {
    uint16_t crc_val;
    if (track_buf.size() == 0) return;
    for (auto it = track_buf.begin(); it != track_buf.end(); ++it) {
        uint8_t data = *it;
        switch (data) {
        case 0xf5:      // marker A1
            write_data(data, true);
            m_crcgen.data(0xa1);
            break;
        case 0xf6:      // marker C2
            write_data(data, true);
            m_crcgen.data(0xc2);
            break;
        case 0xf7:      // write CRC (two bytes)
            m_crcgen.data(0x00);
            m_crcgen.data(0x00);
            crc_val = m_crcgen.get();
            write_data(crc_val >> 8);
            write_data(crc_val & 0xffu);
            break;
        case 0xf8:      // data address marks (reset CRC)
        case 0xf9:      // data address marks (reset CRC)
        case 0xfa:      // data address marks (reset CRC)
        case 0xfb:      // data address marks (reset CRC)
        case 0xfe:      // ID address marks (reset CRC)
            m_crcgen.reset();
            write_data(data);
            m_crcgen.data(data);
            break;
        default:
            write_data(data);
            m_crcgen.data(data);
            break;
        }
    }
}

/**
* Read an sector ID.
* Start reading from the current position and read the 1st found sector ID
*
* @param[in] none
* @param[out] id_field read sector ID data
* @param[out] crc_error crc error status
* @return none
*/
void fdc_bitstream::read_id(std::vector<uint8_t>& id_field, bool& crc_error) {
    uint8_t read_data, mc_byte = 0;
    bool missing_clock;
    size_t read_count = 0;
    id_field.clear();
    crc_error = false;
    size_t total_read_count = 0;
    while (true) {
        switch (m_state) {
        case fdc_state::IDLE:
            m_codec.mfm_read_byte(read_data, missing_clock, false, false);
            if (missing_clock) {
                m_state = fdc_state::CHECK_MARK;
                mc_byte = read_data;
            }
            break;
        case fdc_state::CHECK_MARK:
            m_codec.mfm_read_byte(read_data, missing_clock, false, false);
            if (missing_clock) {
                mc_byte = read_data;
            }
            else if (mc_byte == 0xa1 && (read_data == 0xfc || read_data == 0xfd || read_data == 0xfe || read_data == 0xff /* IDAMs*/)) {
                id_field.clear();
                id_field.push_back(read_data);
                m_crcgen.reset();
                m_crcgen.data(read_data);
                read_count = 4 + 2;    // ID+CRC
                m_state = fdc_state::READ_IDAM;
            }
            else {
                m_state = fdc_state::IDLE;
            }
            break;
        case fdc_state::READ_IDAM:
            m_codec.mfm_read_byte(read_data, missing_clock, true, true);
            id_field.push_back(read_data);
            m_crcgen.data(read_data);
            if (--read_count == 0) {
                crc_error = ((m_crcgen.get() == 0) ? false : true);
                id_field.erase(id_field.begin());       // remove the ID mark on the top
                m_state = fdc_state::IDLE;
                return;
            }
            break;
        default:
            m_state = fdc_state::IDLE;
            break;
        }
        total_read_count++;
        if (total_read_count > m_codec.get_track_length()) {
            return;
        }
    }
}

/**
* Read a sector.
* Read sector data from the next nearest DAM/DDAM mark from the current position.
* This function doesn't care about the sector ID and simply reads the subsequent sector data.
* Combine the read_id() function to find the desired sector ID and call this function to read the sector body data.
* 
*@param[in] sect_length Sector length code (0-3)
*@param[out] sect_data Read sector data buffer
*@param[out] crc_error CRC error flag (true=error)
*@param[out] dam_type (true=DDAM/false=DAM)
*/
void fdc_bitstream::read_sector(size_t sect_length_code, std::vector<uint8_t>& sect_data, bool& crc_error, bool& dam_type) {
    std::vector<size_t> sector_length_table{ 128, 256, 512, 1024 };
    uint8_t read_data, mc_byte = 0;
    bool missing_clock;
    size_t read_count = 0;
    sect_data.clear();
    crc_error = false;
    dam_type = false;
    size_t total_read_count = 0;
    while (true) {
        switch (m_state) {
        case fdc_state::IDLE:
            m_codec.mfm_read_byte(read_data, missing_clock, false, false);
            if (missing_clock) {
                m_state = fdc_state::CHECK_MARK;
                mc_byte = read_data;
            }
            break;
        case fdc_state::CHECK_MARK:
            m_codec.mfm_read_byte(read_data, missing_clock, false, false);
            if (missing_clock) {
                mc_byte = read_data;
            }
            else if (mc_byte == 0xa1 && (read_data == 0xfa || read_data == 0xfb || read_data == 0xf8 || read_data == 0xf9)) {
                if (read_data == 0xfa || read_data == 0xfb) {     // Normal DAM
                    dam_type = false;
                }
                else if (read_data == 0xf8 || read_data == 0xf9) {     // DDAM
                    dam_type = true;
                }
                sect_data.clear();
                m_crcgen.reset();
                m_crcgen.data(read_data);
                read_count = sector_length_table[sect_length_code & 0b0011u] + 2;    // sector size+CRC
                m_state = fdc_state::READ_SECT;
            }
            else {
                m_state = fdc_state::IDLE;
            }
            break;
        case fdc_state::READ_SECT:
            m_codec.mfm_read_byte(read_data, missing_clock, true, true);
            m_crcgen.data(read_data);
            sect_data.push_back(read_data);
            if (--read_count == 0) {
                crc_error = ((m_crcgen.get() == 0) ? false : true);
                sect_data.pop_back();                     // remove CRC field
                sect_data.pop_back();                     // remove CRC field
                m_state = fdc_state::IDLE;
                return;
            }
            break;
        default:
            m_state = fdc_state::IDLE;
            break;
        }
        total_read_count++;
        if (total_read_count > m_codec.get_track_length()) {
            return;
        }
    }
}

/**
* Write a sector.
* Write sector data from the current position.
* Please be aware that this function follows and emulates the write sequence of the FD179x/MB8876 FDC device.
* This means this function will skip certain bytes from the start position and then start actual data writing that includes SYNC bytes and the data address marks.
* To allow the user program to emulate the 'force interrupt' command of FD179x/MB8876, this function has an option not to write the CRC bytes and the last 0xff byte.
* 
* @param[in] write_data The sector data to write.
* @param[in] dam_type Specify the type of data address mark (true:DDAM, false:DAM(normal))
* @param[in] write_crc The flag to specify whether write the last CRC + $FF, or not.
*/
// dam_type : false=DAM, true=DDAM
void fdc_bitstream::write_sector(std::vector<uint8_t> write_data, bool dam_type, bool write_crc) {
    for (int i = 0; i < 2; i++) m_codec.mfm_write_byte(0x00, false, false);     // WG==false == dummy write
    for (int i = 0; i < 8; i++) m_codec.mfm_write_byte(0x00, false, false);     // WG==false == dummy write
    for (int i = 0; i < 1; i++) m_codec.mfm_write_byte(0x00, false, false);     // WG==false == dummy write
    for (int i = 0; i < 11; i++) m_codec.mfm_write_byte(0x00, false, false);     // WG==false == dummy write
    for (int i = 0; i < 12; i++) m_codec.mfm_write_byte(0x00);                   // 0x00 x 12
    for (int i = 0; i < 3; i++) m_codec.mfm_write_byte(0xf5, true);             // 0xf5 (0xa1*) x 3

                                                                                    // reset CRC
    m_crcgen.reset();

    // write DAM/DDAM
    if (dam_type == false) {
        m_codec.mfm_write_byte(0xfb);     // 0xfb (normal DAM) x 1
        m_crcgen.data(0xfb);
    }
    else {
        m_codec.mfm_write_byte(0xf8);     // 0xf8 (Deleted DAM) x 1
        m_crcgen.data(0xf8);
    }

    // write sector data
    for (auto it = write_data.begin(); it != write_data.end(); ++it) {
        uint8_t data = *it;
        m_codec.mfm_write_byte(data);
        m_crcgen.data(data);
    }

    // write CRC
    if (write_crc == true) {
        m_crcgen.data(0x00);
        m_crcgen.data(0x00);
        uint16_t crc_val = m_crcgen.get();
        m_codec.mfm_write_byte(crc_val >> 8);
        m_codec.mfm_write_byte(crc_val & 0xffu);

        m_codec.mfm_write_byte(0xff);           // write 0xff (or 0x4f)
    }
}

/**
* Set current bit position in the track buffer.
* The position is the bit position in the track buffer.
* When the sampling rate of the track buffer is 4MHz, one bit means 250ns (1/4MHz).
* When you set the position to 10 and the sampling rate is 4MHz, the new position will be 2.5us from the top of the track buffer. (1/4MHz * 10 = 2.5us)
*
* @param[in] bit_pos New position
*/
void fdc_bitstream::set_pos(size_t bit_pos) {
    m_codec.set_pos(bit_pos);
    clear_wraparound();
}

/**
* Get current position in the track buffer.
* 
* @param[in] none
* @return size_t Current bit position in the track buffer.
*/
size_t fdc_bitstream::get_pos(void) {
    return m_codec.get_pos();
}

/**
* Set a new track data.
* Set a new track data in new a bit_array.
* 
* @param[in] track_data New track data.
* @return none
*/
void fdc_bitstream::set_raw_track_data(bit_array track_data) {
    m_codec.set_track_data(track_data);
}

/**
* Write a byte data to the track buffer at the current position and advance the position for one byte.
* This function writes a byte data to the track buffer. The data wil be written at the current position, and the pointer will advance for one byte.
* This function also supports FD179x/MB8877 special codes. When the 'mode' is set to 'true', the special codes are recognized and handled respectively.
* When the 'write_gate' is set to 'false', this function will not perform actual data writing but advance the pointer. You can use this feature for dummy data writing (or just skip certain bytes).
*
* @param[in] data A byte data to write.
* @param[in] mode Special code handling mode (true=special, false=normal)
* @param[in] write_gate Flag to decide whether the actual write is performed or not.
*/
void fdc_bitstream::write_data(uint8_t data, bool mode, bool write_gate) {
    m_codec.mfm_write_byte(data, mode, write_gate);
}