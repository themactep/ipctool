#include <stdint.h>

#include "common.h"

#include "vendors/anjoy.h"
#include "vendors/buildroot.h"
#include "vendors/hankvision.h"
#include "vendors/openwrt.h"
#include "vendors/ruision.h"
#include "vendors/sstar.h"
#include "vendors/xm.h"

bool get_board_id() {
    if (is_xm_board()) {
        gather_xm_board_info();
        return true;
    } else if (is_openwrt_board()) {
        gather_openwrt_board_info();
        return true;
    } else if (is_br_board()) {
        gather_br_board_info();
        return true;
    } else if (is_ruision_board()) {
        gather_ruision_board_info();
        return true;
    } else if (is_hankvision_board()) {
        gather_hankvision_board_info();
        return true;
    } else if (is_anjoy_board()) {
        gather_sstar_board_info();
        gather_anjoy_board_info();
        return true;
    } else if (is_sstar_board()) {
        gather_sstar_board_info();
        return true;
    }
    return false;
}
