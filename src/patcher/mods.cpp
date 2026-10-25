#include "mods.h"

#include "setup_log.h"
#include "text.h"

#include <windows.h>

#include <cstring>
#include <map>

namespace
{
// See docs/MODS.md for what each mod does. The offsets are file offsets; in manager.exe the VA is the offset plus
// 0x400000. No two mods that change the same bytes can be installed together.
const std::vector<Mod> kMods = {
    // Youth players from any country: the youth search (0x48DCF0) takes the players of the hidden team Juveniles
    // Españoles and checks each one (0x48D9E0). For a search made for a team, the check also rejects the players
    // whose country isn't the team's, and those the team's competition rules out (its rule on foreign players). Skip
    // that block, as for a search made for no team; the other conditions stay: not already in the team, 18 or
    // younger, and the positions searched.
    {L"juveniles-de-cualquier-pais", L"Jóvenes promesas de cualquier país",
     L"La búsqueda de jóvenes promesas encuentra juveniles de cualquier país, no solo del país del club, y sin las "
     L"reglas de extranjeros de su competición.",
     L"manager.exe",
     {{0x8DA2B,
       {0x0F, 0x84, 0xA6, 0x00, 0x00, 0x00},     // je 0x48DAD7 (search made for no team)
       {0xE9, 0xA7, 0x00, 0x00, 0x00, 0x90}}}},  // jmp 0x48DAD7; nop
    // No limit on foreign players in the squad: every limit check (signings, offers, youth promotion, the other
    // clubs' transfers) compares the club's foreign players, counted by 0x49A7E0, with its competition's maximum
    // (virtual method +0x8C). Count none; and in the three competitions whose maximum is 0, point that method at
    // the one the competitions without a limit use (0x53FD80, 65535). Also lets foreign players be offered to clubs
    // that refused them.
    {L"extranjeros-sin-limite-en-plantilla", L"Sin límite de extranjeros en la plantilla",
     L"Se pueden fichar todos los extranjeros que se quiera, también en las categorías que no los admiten. Vale para "
     L"todos los clubes.",
     L"manager.exe",
     {{0x9A7E0, {0x51, 0x64, 0x8B}, {0x33, 0xC0, 0xC3}},       // push ecx; mov edx,fs:[...] -> xor eax,eax; ret
      {0x67A728, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7A69C +0x8C: 0x40CE30 (0) -> 0x53FD80
      {0x67CC6C, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7CBE0 +0x8C
      {0x67CF84, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7CEF8 +0x8C
      {0x1064D8, {0x74, 0x20}, {0xEB, 0x20}}}},                // je -> jmp 0x5064FA: no "No puedes ofrecer..."
    // No limit on foreign players in line-ups: the six calls of the competition's line-up maximum (virtual method
    // +0x94) return 65535 instead: confirming the line-up (0x969068), swapping players (0x41F4F8) and the automatic
    // line-ups (the other four). Each 6-byte call becomes a 6-byte mov.
    {L"extranjeros-sin-limite-en-alineacion", L"Sin límite de extranjeros en las alineaciones",
     L"Se pueden alinear todos los extranjeros que se quiera. Vale para todos los clubes, también en sus alineaciones "
     L"automáticas.",
     L"manager.exe",
     {{0x1F4F8, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},  // call [edx+94h] -> mov eax,0FFFFh
      {0x93470, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x93CBF, {0xFF, 0x90, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},  // call [eax+94h]
      {0x944FC, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x95D49, {0xFF, 0x90, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x569068, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}}}},
    // Shirt numbers in every competition: only four competitions answer "uses fixed shirt numbers" (virtual method
    // +0x9C) with true; the others share a merged "return false". The nine calls of it, all on a club's competition,
    // get true instead: the club menu offers Dorsales, line-ups keep the numbers, and the club's processing and its
    // messages go as in those four. Each 6-byte call becomes mov al,1 and four nops. The database has no numbers
    // for the clubs of the other competitions, so their players start with repeated ones (mostly 1).
    {L"dorsales-en-todas-las-competiciones", L"Dorsales en todas las competiciones",
     L"Se pueden elegir los dorsales en todas las ligas y divisiones, no solo en las que los tenían. La base de datos no "
     L"trae dorsales para esos clubes: sus jugadores empiezan con números repetidos hasta que se eligen.",
     L"manager.exe",
     {{0x18EAE, {0xFF, 0x90, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},  // club menu: item count
      {0x19052, {0xFF, 0x90, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},  // club menu: Dorsales
      {0x93A2E, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},  // line-ups: no renumbering
      {0x940AC, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},
      {0x9D42A, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},
      {0x9B27F, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},  // club processing
      {0x9B8F7, {0xFF, 0x90, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},  // messages
      {0x9C4B4, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}},
      {0xAC848, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90}}}},
    // Every player's flag in lists: the lists draw a player's flag (0x4C5D30, by his nationality) only if he is
    // foreign in the current competition (0x4B1C90, or the competition's +0xC8 directly), and otherwise jump over the
    // block that draws it. That jump becomes nops in the fifteen places; each block only draws the flag.
    {L"banderas-de-todos-los-jugadores", L"Banderas de todos los jugadores",
     L"Las listas de jugadores muestran la bandera de todos, también la de los del país de la competición, no solo la "
     L"de los extranjeros.",
     L"manager.exe",
     {{0x2BF1, {0x74, 0x62}, {0x90, 0x90}},  // after the foreign test at 0x402BEA
      {0x36871, {0x74, 0x7D}, {0x90, 0x90}},  // 0x43686A
      {0x3E112, {0x74, 0x5C}, {0x90, 0x90}},  // 0x43E10B
      {0x12484E, {0x74, 0x62}, {0x90, 0x90}},  // 0x524847
      {0x126955, {0x74, 0x57}, {0x90, 0x90}},  // 0x52694E
      {0x3DF10F, {0x74, 0x60}, {0x90, 0x90}},  // 0x7DF108
      {0x4C9D12, {0x74, 0x5C}, {0x90, 0x90}},  // 0x8C9D0B
      {0x559599, {0x74, 0x5C}, {0x90, 0x90}},  // 0x959592
      {0x3331A, {0x0F, 0x84, 0x85, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x433313
      {0x35DD5, {0x0F, 0x84, 0x82, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x435DCE
      {0x361CC, {0x0F, 0x84, 0x82, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x4361C5
      {0x5234C, {0x0F, 0x84, 0x84, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x452345 (Jóvenes promesas)
      {0x52795, {0x0F, 0x84, 0x88, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x45278E (Jóvenes promesas)
      {0x11F6EF, {0x0F, 0x84, 0x85, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},  // 0x51F6E8
      {0x11AF1F, {0x0F, 0x84, 0x80, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}}}},  // competition +0xC8 at 0x51AF17
    // Country of birth for everyone: the database loader (0x4AE2C0) keeps a player's country of birth (+0x13E) only
    // if the database marks him as nationalised, and writes 0 otherwise. Keep it for every player: those born
    // outside their nationality's country become domestic in both countries for the foreign-player rules (the
    // foreign test, 0x4B1C90, needs both countries to be foreign), as the nationalised ones already are. Applies to
    // games started after installing it.
    {L"pais-de-nacimiento-para-todos", L"País de nacimiento para todos",
     L"Los jugadores nacidos en un país distinto del de su nacionalidad no cuentan como extranjeros en ninguno de los "
     L"dos países, ni en el de su nacionalidad ni en el de nacimiento, como ya pasaba con los nacionalizados. Vale "
     L"para las partidas que se empiecen después de instalarlo.",
     L"manager.exe",
     {{0xAE4D1, {0x75, 0x07}, {0xEB, 0x07}}}},  // jne -> jmp 0x4AE4DA: not nationalised no longer clears +0x13E
    // Flag of the country of birth: where a player list reads the nationality for the flag (xor reg,reg; mov
    // regL,[player+61h], 5 bytes), call a helper instead (5 bytes) that returns the country of birth (+0x13E) when
    // there is one, and the nationality otherwise. One 15-byte helper per pair of registers, each in a 15-byte
    // padding run: movzx reg,[player+13Eh]; test reg,reg; jnz ret; mov regL,[player+61h]; ret.
    {L"bandera-del-pais-de-nacimiento", L"Bandera del país de nacimiento",
     L"Las listas de jugadores muestran la bandera del país de nacimiento en vez de la de la nacionalidad, cuando se "
     L"conoce: la de los nacionalizados, o la de todos con \"País de nacimiento para todos\".",
     L"manager.exe",
     {// The helpers, in padding after the functions ending at 0x4B42C0, 0x4B45E0, 0x4B5080, 0x4B5C60, 0x4B6A10,
      // 0x4B6A40 and 0x4B7380.
      {0xB42C1, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x97, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xD2, 0x75, 0x03, 0x8A, 0x57, 0x61, 0xC3}},  // edi -> edx
      {0xB45E1, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x87, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x03, 0x8A, 0x47, 0x61, 0xC3}},  // edi -> eax
      {0xB5081, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x8F, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xC9, 0x75, 0x03, 0x8A, 0x4F, 0x61, 0xC3}},  // edi -> ecx
      {0xB5C61, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x85, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x03, 0x8A, 0x45, 0x61, 0xC3}},  // ebp -> eax
      {0xB6A11, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x82, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x03, 0x8A, 0x42, 0x61, 0xC3}},  // edx -> eax
      {0xB6A41, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x93, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xD2, 0x75, 0x03, 0x8A, 0x53, 0x61, 0xC3}},  // ebx -> edx
      {0xB7381, std::vector<uint8_t>(15, 0x90),
       {0x0F, 0xB6, 0x90, 0x3E, 0x01, 0x00, 0x00, 0x85, 0xD2, 0x75, 0x03, 0x8A, 0x50, 0x61, 0xC3}},  // eax -> edx
      // The fourteen places: xor reg,reg; mov regL,[player+61h] -> call the helper.
      {0x2BF7, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0xC5, 0x16, 0x0B, 0x00}},    // 0x402BF7 -> 0x4B42C1
      {0x3333C, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0x80, 0x0F, 0x08, 0x00}},   // 0x43333C -> 0x4B42C1
      {0x35DE1, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0xDB, 0xE4, 0x07, 0x00}},   // 0x435DE1 -> 0x4B42C1
      {0x361D8, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0xE4, 0xE0, 0x07, 0x00}},   // 0x4361D8 -> 0x4B42C1
      {0x52371, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0x4B, 0x1F, 0x06, 0x00}},   // 0x452371 -> 0x4B42C1
      {0x4C9D18, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0xA4, 0xA5, 0xBE, 0xFF}},  // 0x8C9D18 -> 0x4B42C1
      {0x55959F, {0x33, 0xD2, 0x8A, 0x57, 0x61}, {0xE8, 0x1D, 0xAD, 0xB5, 0xFF}},  // 0x95959F -> 0x4B42C1
      {0x3E118, {0x33, 0xC0, 0x8A, 0x47, 0x61}, {0xE8, 0xC4, 0x64, 0x07, 0x00}},   // 0x43E118 -> 0x4B45E1
      {0x527BA, {0x33, 0xC0, 0x8A, 0x47, 0x61}, {0xE8, 0x22, 0x1E, 0x06, 0x00}},   // 0x4527BA -> 0x4B45E1
      {0x36879, {0x33, 0xC9, 0x8A, 0x4F, 0x61}, {0xE8, 0x03, 0xE8, 0x07, 0x00}},   // 0x436879 -> 0x4B5081
      {0x11F6F9, {0x33, 0xC0, 0x8A, 0x45, 0x61}, {0xE8, 0x63, 0x65, 0xF9, 0xFF}},  // 0x51F6F9 -> 0x4B5C61
      {0x124860, {0x33, 0xC0, 0x8A, 0x42, 0x61}, {0xE8, 0xAC, 0x21, 0xF9, 0xFF}},  // 0x524860 -> 0x4B6A11
      {0x12695D, {0x33, 0xD2, 0x8A, 0x53, 0x61}, {0xE8, 0xDF, 0x00, 0xF9, 0xFF}},  // 0x52695D -> 0x4B6A41
      {0x3DF119, {0x33, 0xD2, 0x8A, 0x50, 0x61}, {0xE8, 0x63, 0x82, 0xCD, 0xFF}}}},  // 0x7DF119 -> 0x4B7381
    // Youth players training without limit: the Entrenando button of Jóvenes promesas (0x451B40) turns a player's
    // training on only while fewer players train (0x49F700) than the youth coach's capacity (0x49F6E0: 1 to 4 by
    // his quality), and shows "No puedes entrenar a más jugadores" otherwise. Nothing else checks the capacity.
    {L"juveniles-entrenando-sin-limite", L"Juveniles entrenando sin límite",
     L"El entrenador de juveniles puede entrenar a todos los jóvenes promesas a la vez, no solo a entre 1 y 4 según su "
     L"calidad. Sigue haciendo falta tener entrenador de juveniles.",
     L"manager.exe",
     {{0x51D48, {0x73, 0x23}, {0x90, 0x90}}}},  // jae 0x451D6D (the message) -> nops
    // Seven substitutes in every competition: a match's bench size is its +0x24, which the line-up screen
    // (0x41CA64) and the automatic line-ups (0x49391C) read. Every match is created with 7; some competitions then
    // set 5 just before the match is played (0x6083B0). Those 23 stores set 7 instead, and so does the automatic
    // line-up's fallback when there's no current match.
    {L"siete-suplentes-en-todas-las-competiciones", L"Siete suplentes en todas las competiciones",
     L"Todas las competiciones permiten siete suplentes en el banquillo, no solo algunas. Vale para todos los "
     L"clubes, también en sus alineaciones automáticas.",
     L"manager.exe",
     {{0x133AE6, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x533AE6
      {0x1389DA, {0xC7, 0x47, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x47, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x5389DA
      {0x140EC4, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x540EC4
      {0x14138D, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x54138D
      {0x141C02, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x541C02
      {0x141D01, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x541D01
      {0x14202A, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x54202A
      {0x1426B7, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x5426B7
      {0x14F77E, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x54F77E
      {0x14FF43, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x54FF43
      {0x15A484, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55A484
      {0x15A94D, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55A94D
      {0x15B1CF, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55B1CF
      {0x15B2CE, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55B2CE
      {0x15B5F9, {0xC7, 0x41, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x41, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55B5F9
      {0x15BC97, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x55BC97
      {0x165786, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x565786
      {0x165C63, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x565C63
      {0x16650F, {0xC7, 0x42, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x42, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x56650F
      {0x16660F, {0xC7, 0x40, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x40, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x56660F
      {0x166945, {0xC7, 0x40, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x40, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x566945
      {0x27D710, {0xC7, 0x46, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x46, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x67D710
      {0x27EB03, {0xC7, 0x46, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x46, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x67EB03
      {0x93921, {0xB8, 0x05, 0x00, 0x00, 0x00}, {0xB8, 0x07, 0x00, 0x00, 0x00}}}},  // 0x493921: mov eax,5 (no match)
};

bool Matches(const std::vector<uint8_t>& bytes, size_t offset, const std::vector<uint8_t>& expected)
{
    return offset + expected.size() <= bytes.size() && memcmp(&bytes[offset], expected.data(), expected.size()) == 0;
}

bool HasBytes(const Mod& mod, const std::vector<uint8_t>& bytes, bool installed)
{
    for (const auto& change : mod.changes)
    {
        if (!Matches(bytes, change.offset, installed ? change.patched : change.original))
            return false;
    }
    return true;
}

void WriteMod(std::vector<uint8_t>& bytes, const Mod& mod, bool install)
{
    for (const auto& change : mod.changes)
    {
        const auto& to = install ? change.patched : change.original;
        memcpy(&bytes[change.offset], to.data(), to.size());
    }
}

bool Overlap(const Mod& a, const Mod& b)
{
    if (_wcsicmp(a.file, b.file) != 0)
        return false;
    for (const auto& x : a.changes)
    {
        for (const auto& y : b.changes)
        {
            if (x.offset < y.offset + y.original.size() && y.offset < x.offset + x.original.size())
                return true;
        }
    }
    return false;
}

struct ModFile
{
    std::vector<uint8_t> bytes;  // as found
    std::vector<uint8_t> base;   // without mods: as the patch leaves it, if the check passed
};

// Reads every file the mods change, and checks that without its mods each is as this version of the patch leaves
// it. Sets an error text for the player otherwise.
bool ReadModFiles(const std::filesystem::path& gameFolder, std::map<std::wstring, ModFile>& files, std::wstring& error)
{
    for (const auto& mod : kMods)
    {
        if (files.count(mod.file))
            continue;
        ModFile& file = files[mod.file];
        if (!ReadBytes(gameFolder / mod.file, file.bytes))
        {
            error = Format(L"No se encuentra PC FÚTBOL 2001 en esta carpeta (falta %s).", mod.file);
            return false;
        }
        file.base = file.bytes;
        if (!RemoveInstalledMods(mod.file, file.base) || HexSha256(file.base.data(), file.base.size()) != PatchedSha256(mod.file))
        {
            error = Format(L"%s no es el que deja el parche %hs. Los mods se instalan sobre el parche: aplícalo antes con "
                           L"pcf2001-patch.exe, de la misma versión que este programa.",
                           mod.file, PATCH_VERSION);
            return false;
        }
    }
    return true;
}
}  // namespace

const std::vector<Mod>& Mods()
{
    return kMods;
}

bool RemoveInstalledMods(const wchar_t* fileName, std::vector<uint8_t>& bytes)
{
    for (const auto& mod : kMods)
    {
        if (_wcsicmp(mod.file, fileName) == 0 && HasBytes(mod, bytes, true))
            WriteMod(bytes, mod, false);
    }
    // Only now: a mod's original bytes may have been another's, installed, until that one was undone.
    for (const auto& mod : kMods)
    {
        if (_wcsicmp(mod.file, fileName) == 0 && !HasBytes(mod, bytes, false))
            return false;
    }
    return true;
}

ModsStatus CheckMods(const std::filesystem::path& gameFolder)
{
    ModsStatus status;
    std::map<std::wstring, ModFile> files;
    if (gameFolder.empty())
    {
        status.details = L"Elige la carpeta donde está instalado el juego.";
        return status;
    }
    if (!ReadModFiles(gameFolder, files, status.details))
        return status;
    // ReadModFiles checked every mod's original bytes in the file without mods, so a mod is either installed or not.
    size_t installed = 0;
    for (const auto& mod : kMods)
    {
        status.installed.push_back(HasBytes(mod, files[mod.file].bytes, true));
        if (status.installed.back())
            ++installed;
    }
    status.ready = true;
    status.details = Format(L"PC FÚTBOL 2001 v3.6 con el parche %hs. Mods instalados: %zu de %zu.", PATCH_VERSION, installed,
                            kMods.size());
    return status;
}

bool SetMods(const std::filesystem::path& gameFolder, const std::vector<bool>& wanted, std::wstring& error)
{
    if (wanted.size() != kMods.size())
    {
        error = L"Lista de mods incorrecta.";
        return false;
    }
    for (size_t i = 0; i < kMods.size(); ++i)
    {
        for (size_t j = i + 1; j < kMods.size(); ++j)
        {
            if (wanted[i] && wanted[j] && Overlap(kMods[i], kMods[j]))
            {
                error = Format(L"\"%s\" y \"%s\" cambian lo mismo y no se pueden instalar juntos.", kMods[i].name, kMods[j].name);
                return false;
            }
        }
    }
    std::map<std::wstring, ModFile> files;
    if (!ReadModFiles(gameFolder, files, error))
        return false;
    if (GameIsRunning(gameFolder))
    {
        error = L"Cierra PC FÚTBOL 2001 antes de cambiar los mods.";
        return false;
    }

    std::error_code fsError;
    std::filesystem::create_directories(gameFolder / kPatchFolder, fsError);
    OpenSetupLog(gameFolder / kPatchFolder / L"pcf2001-mods.log");
    SetupLog(Format(L"pcf2001-mods %hs", PATCH_VERSION));

    // Each file is rebuilt from its contents without mods, with the wanted ones, and written only if it changes.
    for (auto& [name, file] : files)
    {
        std::vector<uint8_t> result = file.base;
        for (size_t i = 0; i < kMods.size(); ++i)
        {
            if (wanted[i] && name == kMods[i].file)
                WriteMod(result, kMods[i], true);
        }
        if (result == file.bytes)
            continue;
        if (!WriteBytes(gameFolder / name, result.data(), result.size()))
        {
            error = Format(L"No se puede modificar %s.", name.c_str());
            SetupLog(Format(L"ERROR: cannot write %s", name.c_str()));
            return false;
        }
        for (size_t i = 0; i < kMods.size(); ++i)
        {
            if (name != kMods[i].file)
                continue;
            if (HasBytes(kMods[i], file.bytes, true) != wanted[i])
                SetupLog(Format(L"%s: %s", kMods[i].id, wanted[i] ? L"installed" : L"removed"));
        }
    }

    const ModsStatus after = CheckMods(gameFolder);
    if (!after.ready)
    {
        error = after.details;
        return false;
    }
    for (size_t i = 0; i < kMods.size(); ++i)
    {
        if (after.installed[i] != wanted[i])
        {
            error = Format(L"\"%s\" no ha quedado como se pedía.", kMods[i].name);
            return false;
        }
    }
    return true;
}
