#pragma once

#include <vector>

/*
CRC calculation routine

CRC polynomial = CCITT - 16  G(X) = 1 + X ^ 5 + X ^ 12 + X ^ 16 == 1 001 000 0010 001 == 0x11021
FDC calculates CRC from the top of the address marks(0xA1) to CRC values.
When FDC generates CRC, the CRC field(2 bytes) are set to 0x00, 0x00 and the initial CRC value is 0x84Cf.
However, the top of address marks can't be read precisely when reading the data from the disc. I decided to skip the top x3 0xA1 values and start CRC caluculation from (F8/FB/FC/FE) with CRC initial value of 0xe59a.

If the data are read correctly, the return value will be 0x0000.
data = [0xfe, 0x01, 0x01, 0x03, 0x01, 0xdd, 0xea]
crc_value = crc.data(data[:])  # crc_value must be 0
*/

class fdc_crc {
private:
	const uint32_t m_polynomial = 0b0001000100000010000100000000;   // 0001 0001 0000 0010 0001 [0000 0000]
	uint32_t       m_crc_val;
public:
	fdc_crc() { reset();  };

	inline void reset(size_t type = 0)
	{
		switch (type) {
		case 1:  m_crc_val = 0x84cf00; break;  // In case AM = A1 A1 A1 + F8/FB/FC/FE
		default: m_crc_val = 0xe59a00; break;  // In case the 1st x3 A1s are omitted ( F8/FB/FC/FE )
		}
	}

	inline uint16_t get(void) { return (m_crc_val >> 8) & 0x0ffffu; }
	
	void data(uint8_t dt) {
		m_crc_val |= dt;
		for (size_t i = 0; i < 8; i++) {
			m_crc_val <<= 1;
			if (m_crc_val & 0x1000000) {
				m_crc_val ^= m_polynomial;
			}
		}
	}

	void data(std::vector<uint8_t> &buf) {
		for (auto it = buf.begin(); it != buf.end(); ++it) {
			data(static_cast<uint8_t>(*it));
		}
	}
};
