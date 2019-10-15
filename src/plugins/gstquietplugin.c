#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstquietdec.h"
#include "gstquietenc.h"

gboolean
quiet_init (GstPlugin * quiet)
{
  if (!quietdec_init (quiet))
    return FALSE;
  if (!quietenc_init (quiet))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    quiet,
    "Quiet MODEM plugin",
    quiet_init, PACKAGE_VERSION, GST_LICENSE, PACKAGE_NAME, PACKAGE_URL)
