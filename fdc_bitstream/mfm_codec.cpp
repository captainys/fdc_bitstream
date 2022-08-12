/**
 * @file mfm_codec.cpp
 * @brief MFM encoder/decoder
 * @author Yasunori Shimura
 * 
 * @details MFM codec provides encoding and decoding feature to read and write a raw floppy bit stream data. 
 *
 * @copyright Copyright (c) 2022
 */

#include "mfm_codec.h"

/**
 * @brief Construct a new mfm codec::mfm codec object
 * 
 */
mfm_codec::mfm_codec() : m_bit_stream(0),
        m_sync_mode(false), m_wraparound(false),
        m_prev_write_bit(0),
        m_sampling_rate(4e6), m_data_bit_rate(500e3),
        m_fluctuation(false), m_fluctuator_numerator(1), m_fluctuator_denominator(1),
        m_track_ready(false)
{
    update_parameters();
    set_vfo_gain(0.1f, 10.f);           // Set data separator VFO gain (low=non-SYNC and high=SYNC period)
    set_gain(m_vfo_gain_l);
}

/**
 * @brief Reset MFM decoder parameters
 * 
 * @param[in] None
 * @param[out] None
 */
void mfm_codec::reset(void) {
    m_sampling_rate = 4e6;
    m_data_bit_rate = 500e3;
    update_parameters();
    m_prev_write_bit = 0;
    m_sync_mode = false;
    clear_wraparound();
    m_fluctuation = false;
    m_fluctuator_numerator = 1;
    m_fluctuator_denominator = 1;
    m_track.clear_array();
    m_track_ready = false;
    set_vfo_gain(1.f, 10.f);
}

/**
 * @brief Set a new track data.
 * 
 * @param track Track bit array data.
 */
void mfm_codec::set_track_data(bit_array track) {
    m_track = track;
    m_track.set_stream_pos(0);
    m_track.clear_wraparound_flag();
    if (track.size() > 0) {
        m_track_ready = true;
    }
    else {
        m_track_ready = false;
    }
}

/**
 * @brief Get the track data.
 *
 * @return bit_array Return track data in bit_array.
 */
bit_array mfm_codec::get_track_data(void) {
    return m_track;
}

/**
 * @brief Unset track data. Track data will be marked as unavailable. You need to set another track bit array data before you performs read/write operations.
 * 
 */
void mfm_codec::unset_track_data(void) {
    m_track.clear_wraparound_flag();
    m_track_ready = false;
}

//inline bool is_wraparound(void) { return m_wraparound; }
//inline void clear_wraparound(void) { m_wraparound = false; }
//inline size_t get_track_length(void) { return m_track.get_length(); }       // unit = bit

/**
 * @brief Set new bit cell size.
 *   012345678
 *   | WWWW  |
 *     <-->    Window size (4)
 *   <->       Window ofst (2)
 *   <------>  Cell size   (8)
 * 
 * @param cell_size New bit cell size (unit=bits)
 */
void mfm_codec::set_cell_size(double cell_size) {
    m_bit_cell_size = cell_size;
    m_data_window_size = cell_size / 2.f;
    if (m_data_window_size < 0.5f) {          // Avoid too narrow m_data_window_size situation
        m_data_window_size = 0.5f;
    }
    m_data_window_ofst = cell_size / 4.f;
#ifdef DEBUG
    std::cout << "Bit cell size:" << m_bit_cell_size << std::endl;
    std::cout << "Data window size:" << m_data_window_size << std::endl;
    std::cout << "Data window offset:" << m_data_window_ofst << std::endl;
#endif
}


/**
 * @brief Update data cell parameters
 * 
 */
void mfm_codec::update_parameters(void) {
    double cell_size = m_sampling_rate / m_data_bit_rate;
    m_bit_cell_size_ref = cell_size;
    set_cell_size(cell_size);
}

/**
 * @brief Set new data bit rate for the bit array buffer.
 * 
 * @param data_bit_rate New bit rate in bit/sec unit. (MFM/2D = 500KHz = 500e3)
 */
void mfm_codec::set_data_bit_rate(size_t data_bit_rate) {
    m_data_bit_rate = data_bit_rate;
    update_parameters();
}

/**
 * @brief Set new sampling rate for the bit array buffer.
 * 
 * @param sampling_rate Sampling rate in Hz. (e.g. 4MHz = 4e6)
 */
void mfm_codec::set_sampling_rate(size_t sampling_rate) { 
    m_sampling_rate = sampling_rate; 
    update_parameters(); 
}


// ---------- Data separator

// How data separator work: 
// 
// | WWWW | WWWW | WWWW | WWWW | WWWW | WWWW | WWWW | WWWW |   <- Data window(W), Data cell boundary(|)
//     P           P           P         P         P           <- Data pulses
//     1     0      1      0       0      1      0      0      <- Data reading (only the data pulses within the data window will be considered) 
//
// 01234567
// | WWWW  |  <= In this case, bit cell size = 8, data window ofst = 2, data window size = 4
//

/**
 * @brief Read a bit from the bit array track data. 
 *        This function includes data separator and PLL.
 * 
 * @return int Read bit data.
 */
int mfm_codec::read_bit_ds(void) {
    int bit_reading = 0;
    double cell_center;
    size_t loop_count = 0;      // for timeout check

    if (is_track_ready() == false) {
        return -1;
    }

    do {
        // check if the next bit is within the next data window
        if (m_distance_to_next_pulse < m_bit_cell_size) {
            // check pulse position
            if (m_distance_to_next_pulse >= m_data_window_ofst &&
                // regular pulse (within the data window)
                m_distance_to_next_pulse < m_data_window_ofst + m_data_window_size) {
                bit_reading = 1;
            }
            else {
                // irregular pulse
#ifdef DEBUG
                std::cout << '?';
#endif
            }
            // Adjust pulse phase (imitate PLL/VFO operation)
            // Limit the PLL/VFO operation frequency and introduce fluctuation with the random generator (certain fluctuation is required to reproduce some copy protection)
            if (m_fluctuation == false || m_rnd() % m_fluctuator_denominator >= m_fluctuator_numerator) {
                cell_center = m_data_window_ofst + m_data_window_size / 2.f;
                double error = m_distance_to_next_pulse - cell_center;

                // Data pulse position adjustment == phase correction
                m_distance_to_next_pulse -= error * 0.05f * m_vfo_gain;

                // Cell size adjustment == frequency correction
                double new_cell_size = m_bit_cell_size + error * 0.01f * m_vfo_gain;
                // Limit the range of cell size
                if (new_cell_size < m_bit_cell_size_ref * 0.8) new_cell_size = m_bit_cell_size_ref * 0.8;
                if (new_cell_size > m_bit_cell_size_ref * 1.2) new_cell_size = m_bit_cell_size_ref * 1.2;
                set_cell_size(new_cell_size);
            }
            size_t distance = m_track.distance_to_next_bit1();
#if 0
            // give timing fluctuation (intentionally - to immitate the spndle rotation fluctuation)
            size_t rand_num = m_rnd() % 128;
            if (rand_num == 0) {
                distance++;
            }
            else if (rand_num == 1) {
                distance--;
            }
#endif
            m_distance_to_next_pulse += distance;

            if (loop_count++ > m_bit_cell_size) {       // time out
                return -1;
            }
        }
    } while (m_distance_to_next_pulse < m_bit_cell_size);
    // advance bit cell position
    if (m_distance_to_next_pulse >= m_bit_cell_size) {
        m_distance_to_next_pulse -= m_bit_cell_size;
    }
    return bit_reading;
}

/**
 * @brief Set gain for the PLL in the data separator.
 * 
 * @param[in] gain Gain value for the PLL (value in double type).
 */
void mfm_codec::set_gain(double gain) {
    if (gain <= 0.f) gain = 0.01f;
    if (gain > 100.f) gain = 100.f;
    m_vfo_gain = gain;
}


// ----------------------------------------------------------------


/**
 * @brief Read 1 byte from track buffer
 * 
 * @param[out] data Read data of 1 byte. 
 * @param[out] missing_clock Missing clock flag (true=missing clock). 
 * @param ignore_missing_clock Flag to ignore missing clock pattern during decoding (true=ignore).
 * @param ignore_sync_field Flag to ignore SYNC pattern during decoding (true=ignore).
 * @return true: Result is valid.
 * @return false: Track bit array data is not set. Returned values are invalid.
 */
bool mfm_codec::mfm_read_byte(uint8_t& data, bool& missing_clock, bool ignore_missing_clock, bool ignore_sync_field) {
    size_t decode_count = 0;
    missing_clock = false;

    if (is_track_ready() == false) {
        data = 0;
        missing_clock = false;
        return false;
    }

    do {
        int bit_data = read_bit_ds();
        if (bit_data == -1) {
             data = 0;
             missing_clock = false;
             return false;
        }
        decode_count++;
        if (m_track.is_wraparound()) {
            m_track.clear_wraparound_flag();
            m_wraparound = true;
        }
        m_bit_stream = (m_bit_stream << 1) | bit_data;

        if (ignore_missing_clock == false) {
            if ((m_bit_stream & 0x0ffffu) == m_missing_clock_a1) {      // Missing clock 0xA1 pattern
                data = 0xa1;
                missing_clock = true;
                return true;
            }
            if ((m_bit_stream & 0x0ffffu) == m_missing_clock_c2) {       // Missing clock 0xC2 pattern
                data = 0xc2;
                missing_clock = true;
                return true;
            }
        }
        if (ignore_sync_field == false) {
            if (m_bit_stream == m_pattern_00) {
                decode_count &= ~0b01u;                                 // C/D synchronize
                set_gain(m_vfo_gain_h);
            }
            else {
                set_gain(m_vfo_gain_l);
            }
        }
    } while (decode_count < 16);

    // Extract only 'D' bits (exclude 'C' bits). MFM data is 'CDCDCDCD..CD'.
    uint8_t read_data = 0;
    for (uint16_t bit_pos = 0x4000; bit_pos > 0; bit_pos >>= 2) {
        read_data = (read_data << 1) | (m_bit_stream & bit_pos ? 1 : 0);
    }
    data = read_data;
    missing_clock = false;
    return true;
}


/**
 * @brief Encode a byte with MFM. 
 * 
 * @param data Data to encode.
 * @param mode Encoding mode (true: cares FD179x/MB8877 compatible special codes ($f5, $f6) and generates a data with missing-clock pattern)
 * @return uint16_t Encoded bit pattern data.
 */
uint16_t mfm_codec::mfm_encoder(uint8_t data, bool mode) {
    // Data swap for special code
    uint8_t write_data = data;
    if (mode == true) { // special coding mode (F5 and F6 will be converted to A1* C2* with missing-clock pattern respectively)
        switch (data) {
        case 0xf5:
            write_data = 0xa1;
            break;
        case 0xf6:
            write_data = 0xc2;
            break;
        }
    }
    // MFM encoding
    uint16_t bit_pattern = 0;
    uint16_t mfm_bit_pattern = 0;
    int current_bit;
    int clock_bit;
    for (size_t bit_pos = 0x80; bit_pos != 0; bit_pos >>= 1) {
        current_bit = (write_data & bit_pos) ? 1 : 0;
        clock_bit = (m_prev_write_bit == 0 && current_bit == 0) ? 1 : 0;
        mfm_bit_pattern = (mfm_bit_pattern << 1) | clock_bit;                 // clock
        mfm_bit_pattern = (mfm_bit_pattern << 1) | current_bit;               // data
        m_prev_write_bit = current_bit;
    }
    // Missing clock operation
    if (mode == true) {
        switch (data) {
        case 0xf5:
            mfm_bit_pattern &= ~0b100000u;              // remove a clock bit
            break;
        case 0xf6:
            mfm_bit_pattern &= ~0b10000000u;            // remove a clock bit
            break;
        }
    }
    return mfm_bit_pattern;
}


/**
 * @brief Write a byte to the track bit array data with MFM encoding.
 * 
 * @param data Data to write to the track buffer.
 * @param mode Encoding mode (true: cares FD179x/MB8877 compatible special codes ($f5, $f6) and generates a data with missing-clock pattern)
 * @param write_gate Write gate (true:perform actual write, false:dummy write (no actual write will be perfored. buffer pointer will be increased))
 */
void mfm_codec::mfm_write_byte(uint8_t data, bool mode, bool write_gate) {
    uint16_t bit_pattern = mfm_encoder(data, mode);
    for (uint16_t bit_pos = 0x8000; bit_pos != 0; bit_pos >>= 1) {
        int bit = bit_pattern & bit_pos ? 1 : 0;
        if (write_gate == true) {
            for (size_t i = 0; i < m_bit_width; i++) {
                if (m_bit_width / 2 == i)   m_track.write_stream(bit);    // write data pulse at the center of the bit cell
                else                     m_track.write_stream(0);
            }
        }
        else {
            for (size_t i = 0; i < m_bit_width; i++) {
                m_track.advance_stream_pos();                          // advance stream pointer without actual data write (dummy write)
            }
        }
    }
}

/**
 * @brief Set stream read/write position
 * 
 * @param bit_pos New position (units=bits).
 */
void mfm_codec::set_pos(size_t bit_pos) {
    if (is_track_ready() == false) {
        return;
    }
    m_track.set_stream_pos(bit_pos);
}

/**
 * @brief Get current read/write position
 * 
 * @return size_t Current position (unit=bits).
 */
size_t mfm_codec::get_pos(void) {
    if (is_track_ready() == false) {
        return -1;
    }
    return m_track.get_stream_pos();
}

/**
 * @brief Enable data separator fluctuator.
 *        Data separator has feature to introduce a little uncertainty to reproduce time sensitive copy protect data which relies on the ambiguity of the bit pattern.
 *        PLL will skip working at the rate of (numerator/denominator). If you set it 1/4, the PLL works at rate of 3/4 and stop at rate of 1/4.
 * 
 * @param numerator Numerator to define fluctuate rate.
 * @param denominator Denominator to define fluctuate rate.
 */
void mfm_codec::enable_fluctuator(size_t numerator, size_t denominator) {
    m_fluctuation = true;
    m_fluctuator_numerator = numerator;
    m_fluctuator_denominator = denominator;
}

/**
 * @brief Disable data separator fluctuator.
 * 
 */
void mfm_codec::disable_fluctuator(void) {
    m_fluctuation = false;
}
