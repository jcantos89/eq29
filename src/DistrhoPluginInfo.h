/*
 * DynEQ - Dynamic EQ plugin built with the DISTRHO Plugin Framework
 * ISC style licence (see LICENSE)
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "OpenAudio"
#define DISTRHO_PLUGIN_NAME    "DynEQ"
#define DISTRHO_PLUGIN_URI     "https://github.com/openaudio/dyneq"
#define DISTRHO_PLUGIN_CLAP_ID "studio.openaudio.dyneq"

#define DISTRHO_PLUGIN_BRAND_ID  OpAu
#define DISTRHO_PLUGIN_UNIQUE_ID dNEq

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2

// Key/value states used to persist the band layout between sessions
#define DISTRHO_PLUGIN_WANT_STATE       1

// UI
#define DISTRHO_UI_USER_RESIZABLE       1
#define DISTRHO_UI_DEFAULT_WIDTH        900
#define DISTRHO_UI_DEFAULT_HEIGHT       540

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
