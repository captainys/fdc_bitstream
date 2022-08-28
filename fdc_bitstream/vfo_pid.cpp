#include <iostream>

#include "vfo_pid.h"

void vfo_pid::disp_vfo_status(void) {
    vfo_base::disp_vfo_status();
    std::cout << "-- vfo_pid --" << std::endl;
    std::cout << "Prev pulse pos : " << m_prev_pulse_pos << std::endl;
    std::cout << "Prev phase err : " << m_prev_phase_err << std::endl;
    std::cout << "Phase diff integral : " << m_phase_diff_integral << std::endl;
    std::cout << "Phase err  integral : " << m_phase_err_integral << std::endl;
}

void vfo_pid::reset(void) {
    vfo_base::reset();
    soft_reset();
}

void vfo_pid::soft_reset(void) {
    vfo_base::soft_reset();
    m_prev_pulse_pos = 0.f;
    m_prev_phase_err = 0.f;
    m_phase_err_integral = 0.f;
    m_phase_diff_integral = 0.f;
}

/**
 * @brief Calculate new pulse position based on the current bit cell parameters. Adjust pulse position, and calculate and optimize bit cell parameters.
 * 
 * @param pulse_pos Pulse position (the distance to the next pulse position in bit unit).
 * @return double Adjusted pulse position.
 *            0123456789abcdef0
 *  Bit cell  |               |
 *  Window    |   WWWWWWWW    |
 *  Center    |       ^       |
 *  Pulse     |     |         |
 *  Error     |     |-|       |
 * 
 *  Phase correction : shift next pulse position to adjust the phase.
 *  Freq. correction : change bit cell size by accumlated (integrated) error.
 */
double vfo_pid::calc(double pulse_pos) {
    // compare the position of the current and previous pulses in the bit cell (phase_diff -=slow freq, +=fast freq)
    double phase_diff = m_prev_pulse_pos - pulse_pos;
    // phase correction. assuming phase difference is less than 180deg.
    if(phase_diff > m_cell_size / 2.f) {
        phase_diff = phase_diff - m_cell_size;
    } else if (phase_diff < -(m_cell_size / 2.f)) {
        phase_diff = phase_diff + m_cell_size;
    }

    // Phase error from the center of the bit cell
    double phase_err = m_cell_center - pulse_pos;
    double phase_err_diff = phase_err - m_prev_phase_err;
    m_phase_err_integral += phase_err; 
    m_prev_phase_err = phase_err;

    // Cell size adjustment == frequency correction
    m_phase_diff_integral += phase_diff;
    double new_cell_size = m_cell_size_ref - (m_phase_diff_integral * (1.0f/24.f) ) * m_current_gain - 
                            (phase_err * (1.0f/2.f) - phase_err_diff * (1.0f/8.f) + m_phase_err_integral * (1.0f/128.f));
    constexpr double range = 0.02f; 
    new_cell_size = limit(new_cell_size, m_cell_size_ref * (1-range), m_cell_size_ref * (1+range));
    m_prev_pulse_pos = pulse_pos;

    set_cell_size(new_cell_size);

    return pulse_pos;
}
