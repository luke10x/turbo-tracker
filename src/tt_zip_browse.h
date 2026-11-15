#ifndef TT_ZIP_BROWSE_H
#define TT_ZIP_BROWSE_H

#include "f80_file_dialog.h"

const F80FileBrowseApi *tt_zip_browse_api(void);
/* ZIP browsing plus read-only virtual directories for 32-voice Yamaha SYX banks. */
const F80FileBrowseApi *tt_zip_syx_browse_api(void);

#endif
