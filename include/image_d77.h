#pragma once

#include "dll_export.h"

#include <string.h>

#include "bit_array.h"
#include "image_base.h"

class DLL_EXPORT disk_image_d77 : public disk_image {
private:
public:
    disk_image_d77(void) : disk_image() {};

    void read(const std::string file_name) override;
    void write(const std::string file_name) override;

};