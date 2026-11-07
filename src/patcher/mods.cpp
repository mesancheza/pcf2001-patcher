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
    // set 5 just before the match is played (0x6083B0): 23 times as mov dword ptr [reg+24h],5 and 11 as
    // mov dword ptr [reg+reg+24h],5, among them the Premier League's (0x54997E). Those 34 stores set 7 instead, and so
    // does the automatic line-up's fallback when there's no current match. The pre-season tournaments' 6 (0x5AB20D and
    // four more, from a register they also store elsewhere) stay.
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
      {0x130163, {0xC7, 0x44, 0x31, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x31, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x530163
      {0x1304E6, {0xC7, 0x44, 0x30, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x30, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x5304E6
      {0x137E45, {0xC7, 0x44, 0x3A, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x3A, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x537E45
      {0x1381D7, {0xC7, 0x44, 0x38, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x38, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x5381D7
      {0x14997E, {0xC7, 0x44, 0x07, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x07, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x54997E
      {0x26DC89, {0xC7, 0x44, 0x3A, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x3A, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x66DC89
      {0x26DD41, {0xC7, 0x44, 0x3A, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x3A, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x66DD41
      {0x27D36B, {0xC7, 0x44, 0x38, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x38, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x67D36B
      {0x27EC37, {0xC7, 0x44, 0x06, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x06, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x67EC37
      {0x2877C4, {0xC7, 0x44, 0x29, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x29, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x6877C4
      {0x287989, {0xC7, 0x44, 0x29, 0x24, 0x05, 0x00, 0x00, 0x00}, {0xC7, 0x44, 0x29, 0x24, 0x07, 0x00, 0x00, 0x00}},  // 0x687989
      {0x93921, {0xB8, 0x05, 0x00, 0x00, 0x00}, {0xB8, 0x07, 0x00, 0x00, 0x00}}}},  // 0x493921: mov eax,5 (no match)
    // Signings of a higher level, ×1.5 and ×2: the computer's clubs' signings (0x494AA0) take a player only if his
    // average is within a window around the club's (T). Raise the windows' tops, whose margins are each one
    // instruction's 8-bit immediate: the transfer list (the context's +0x40, up to T+4), the loan list (+0x44, up to
    // T+4), the best player of another club (up to T+2) and a random club's squad (up to T+5, or T+10 when 0x4B0360 is
    // below 25). The bottoms stay, and the free agents have no top. Both change the same bytes, so only one can be
    // installed.
    {L"fichajes-de-mas-nivel", L"Fichajes de más nivel (×1,5)",
     L"Los clubes que lleva el ordenador pueden fichar jugadores más por encima de su propia media: el margen es un 50 % "
     L"mayor que en el juego original (por ejemplo, hasta 6 puntos más en vez de 4). Por abajo no cambia nada. No se "
     L"puede instalar junto con \"Fichajes de más nivel (×2)\".",
     L"manager.exe",
     {{0x94E5C, {0x83, 0xC2, 0x04}, {0x83, 0xC2, 0x06}},   // 0x494E5C add edx,4 -> 6: the transfer list
      {0x951E9, {0x83, 0xC1, 0x04}, {0x83, 0xC1, 0x06}},   // 0x4951E9 add ecx,4 -> 6: the loan list
      {0x954CC, {0x83, 0xC0, 0x02}, {0x83, 0xC0, 0x03}},   // 0x4954CC add eax,2 -> 3: another club's best
      {0x9586A, {0x8D, 0x50, 0x05}, {0x8D, 0x50, 0x08}},   // 0x49586A lea edx,[eax+5] -> 8: a squad
      {0x95871, {0x83, 0xC0, 0x0A}, {0x83, 0xC0, 0x0F}}}},  // 0x495871 add eax,0Ah -> 0Fh: a squad, 0x4B0360 < 25
    {L"fichajes-del-doble-de-nivel", L"Fichajes de más nivel (×2)",
     L"Los clubes que lleva el ordenador pueden fichar jugadores más por encima de su propia media: el margen es el "
     L"doble que en el juego original (por ejemplo, hasta 8 puntos más en vez de 4). Por abajo no cambia nada. No se "
     L"puede instalar junto con \"Fichajes de más nivel (×1,5)\".",
     L"manager.exe",
     {{0x94E5C, {0x83, 0xC2, 0x04}, {0x83, 0xC2, 0x08}},
      {0x951E9, {0x83, 0xC1, 0x04}, {0x83, 0xC1, 0x08}},
      {0x954CC, {0x83, 0xC0, 0x02}, {0x83, 0xC0, 0x04}},
      {0x9586A, {0x8D, 0x50, 0x05}, {0x8D, 0x50, 0x0A}},
      {0x95871, {0x83, 0xC0, 0x0A}, {0x83, 0xC0, 0x14}}}},
    // Transfer-listed players of any level: the computer's clubs' signings (0x494AA0) take a player from the transfer
    // list (the context's +0x40) only if his average is at most the club's (T) plus 4, which a club's whole-squad
    // average keeps far below its best players. Drop that check; the bottom (T-2), the position, the chance, the
    // release clause against the club's +0x124 and the other checks stay.
    {L"transferibles-sin-limite-de-nivel", L"Transferibles sin límite de nivel",
     L"Los clubes que lleva el ordenador pueden hacer ofertas por los jugadores transferibles aunque sean mucho mejores "
     L"que su plantilla: ya no hay un límite por arriba según la media del club. Siguen contando el puesto que buscan, "
     L"su dinero frente a la cláusula del jugador y las demás condiciones.",
     L"manager.exe",
     {{0x94E61, {0x0F, 0x87, 0xC6, 0x02, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}}}},  // ja 0x49512D -> nops
    // Stands of any type: a stand (an amphitheatre's side) stores one of 13 values (0 none, then three each for VIP,
    // Palco, Tribuna and Grada), and the table at 0xA732A8 gives for each the lowest type button allowed: the current
    // one's plus 1. Two places enable the buttons from it, the stand panel's loop (0x4DF550) and the drawing of each
    // button with its label (0x4DF960): only the types above the current one. Enable every type but the current one,
    // in both; the finished work (0x4CD12F) already sets any type, taking off the old seats and adding the new ones.
    // The table is also read elsewhere, so it stays; the address arithmetic around it is compacted to make room.
    {L"gradas-de-cualquier-tipo", L"Gradas de cualquier tipo",
     L"Al remodelar el estadio, una grada puede cambiarse a cualquier tipo, también a uno inferior: una grada VIP "
     L"puede volver a ser Palco, Tribuna o Grada. Cada cambio es una obra como las demás.",
     L"manager.exe",
     {{0xDF853,
       {0xC1, 0xE0, 0x03, 0x8D, 0x0C, 0x88, 0x8B, 0x04, 0x11, 0x39, 0x3C, 0x85, 0xA8, 0x32, 0xA7, 0x00, 0x77, 0x1A},
       {0x8D, 0x04, 0xC2,                          // lea eax,[edx+eax*8]
        0x8B, 0x04, 0x88,                          // mov eax,[eax+ecx*4] (the stand's value; was shl/lea/mov)
        0x8B, 0x0C, 0x85, 0xA8, 0x32, 0xA7, 0x00,  // mov ecx,[eax*4+0A732A8h]
        0x49,                                      // dec ecx (the current type's button, -1 for none)
        0x3B, 0xCF,                                // cmp ecx,edi
        0x74, 0x1A}},                              // je 0x4DF87F (was: ja, every type up to the current one)
      {0xDF98D,
       {0xC1, 0xE0, 0x03, 0x8D, 0x0C, 0x88, 0x8B, 0x04, 0x11, 0x8B, 0x0C, 0x85, 0xA8, 0x32, 0xA7, 0x00, 0x89, 0x4C,
        0x24, 0x10},
       {0x8D, 0x04, 0xC2,                          // lea eax,[edx+eax*8]
        0x8B, 0x04, 0x88,                          // mov eax,[eax+ecx*4]
        0x8B, 0x0C, 0x85, 0xA8, 0x32, 0xA7, 0x00,  // mov ecx,[eax*4+0A732A8h]
        0x49,                                      // dec ecx
        0x89, 0x4C, 0x24, 0x10,                    // mov [esp+10h],ecx (the current type's button)
        0x90, 0x90}},
      {0xDF9AF,
       {0x8D, 0x14, 0x40, 0x8D, 0x04, 0x90, 0x8B, 0x54, 0xC1, 0x64, 0x8B, 0x04, 0x95, 0xA8, 0x32, 0xA7, 0x00, 0x89,
        0x44, 0x24, 0x10},
       {0x6B, 0xC0, 0x0D,                          // imul eax,eax,0Dh (was two lea)
        0x8B, 0x54, 0xC1, 0x64,                    // mov edx,[ecx+eax*8+64h]
        0x8B, 0x04, 0x95, 0xA8, 0x32, 0xA7, 0x00,  // mov eax,[edx*4+0A732A8h]
        0x48,                                      // dec eax
        0x89, 0x44, 0x24, 0x10,                    // mov [esp+10h],eax
        0x90, 0x90}},
      {0xDFA20, {0x77, 0x6D}, {0x74, 0x6D}}}},     // ja -> je 0x4DFA8F: disable only the current type
    // Cafés that can be changed: the 22 places for a bar, cafeteria or restaurant are entries at the stadium's +0x1B8
    // (0x1C bytes, type at +8). The Remodelar panel skips the enable checks for a place that has one (0x4DE52E), and
    // its check (0x8AAFB0) answers the existing type, or -1 for the same type, before the stadium-size checks, which
    // answer -2 or less. Enable a built place too, on any answer of -1 or more. Ordering (0x8AAEB0) already builds
    // over another type, but refuses the same type, which the panel would leave as a pending work with nothing
    // ordered: accept it as a rebuild. The finished work adds 1 to the stadium's count of places (+0x0D), so
    // ordering over a built one takes 1 off it first (new code in the padding at 0x8AB083).
    {L"cafeterias-cambiables", L"Cafeterías cambiables",
     L"Al remodelar el estadio, los bares, cafeterías y restaurantes ya construidos se pueden cambiar por otro tipo, o "
     L"reconstruir, como una obra nueva. Mientras dura la obra, ese local no cuenta.",
     L"manager.exe",
     {{0xDE52E, {0xEB, 0x43}, {0x90, 0x90}},                          // jmp 0x4DE573 (a built place) -> the checks
      {0xDE556, {0x85, 0xC0, 0x75, 0x19}, {0x40, 0x78, 0x1A, 0x90}},  // test/jne -> inc eax; js 0x4DE573: -1 or more
      {0x4AAED2, {0x85, 0xC0}, {0x40, 0x90}},                         // test -> inc eax: the jge takes -1 too
      {0x4AAEE5,
       {0x8D, 0x0C, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x2B, 0xCF, 0x83, 0xFE, 0x03, 0x8D, 0x04, 0x8B, 0x89, 0x70, 0x08,
        0xC7, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00},
       {0x6B, 0xC7, 0x1C,                    // imul eax,edi,1Ch
        0x03, 0xC3,                          // add eax,ebx (the place's entry; was ecx = edi x 7, then lea)
        0xE8, 0x94, 0x01, 0x00, 0x00,        // call 0x8AB083 (take 1 off the count if it has a café)
        0x89, 0x70, 0x08,                    // mov [eax+8],esi (the type)
        0x83, 0x60, 0x04, 0x00,              // and dword ptr [eax+4],0 (weeks)
        0x83, 0xFE, 0x03,                    // cmp esi,3 (for the jne that follows, as before)
        0x90, 0x90, 0x90, 0x90, 0x90}},
      {0x4AB083, std::vector<uint8_t>(12, 0x90),
       {0x83, 0x78, 0x08, 0x01,                    // cmp dword ptr [eax+8],1 (carry only if no café)
        0x80, 0x93, 0x55, 0xFE, 0xFF, 0xFF, 0xFF,  // adc byte ptr [ebx-1ABh],0FFh (stadium +0x0D: -1, or 0 with carry)
        0xC3}}}},                                  // ret
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

bool ModsConflict(size_t a, size_t b)
{
    return a != b && Overlap(kMods[a], kMods[b]);
}

bool RemoveInstalledMods(const wchar_t* fileName, std::vector<uint8_t>& bytes)
{
    // Change by change, not mod by mod: a mod installed by an earlier version of this program may have fewer changes
    // than it has now, and its changes are still undone. Repeated while anything changes, since a change's new bytes
    // may only show once another mod's change over them is undone; bounded, in case two changes undo into each other.
    for (size_t pass = 0; pass <= kMods.size(); ++pass)
    {
        bool undone = false;
        for (const auto& mod : kMods)
        {
            if (_wcsicmp(mod.file, fileName) != 0)
                continue;
            for (const auto& change : mod.changes)
            {
                if (Matches(bytes, change.offset, change.patched))
                {
                    memcpy(&bytes[change.offset], change.original.data(), change.original.size());
                    undone = true;
                }
            }
        }
        if (!undone)
            break;
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
