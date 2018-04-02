//
// Copyright 2018 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include <SpecialK/log.h>
#include <SpecialK/config.h>
#include <SpecialK/utility.h>
#include <SpecialK/parameter.h>
#include <SpecialK/plugin/plugin_mgr.h>

#include <imgui/imgui.h>

extern iSK_INI*             dll_ini;
extern sk::ParameterFactory g_ParameterFactory;


/*
Nino2.exe+11DA450 // Input  timestep (single-precision float, in seconds)
Nino2.exe+11DA454 // Render timestep (single-precision float, in seconds)


=====
// dT (input, render)
//
Nino2.agsInit+2B298D - F3 0F11 35 7FD98500   - movss [Nino2.exe+11DA454],xmm6 { [0.02] }
Nino2.agsInit+2B2995 - F3 0F11 3D 73D98500   - movss [Nino2.exe+11DA450],xmm7 { [0.02] }

F3 0F 11 35 7F D9 85 00 // Variable-timing (Render)
F3 0F 11 3D 73 D9 85 00 // Variable-timing (Input)
-----
90 90 90 90 90 90 90 90 // Fixed-timing (Render)
90 90 90 90 90 90 90 90 // Fixed-timing (Input)

~~~~~
// Game's limiter (Blocks Win32 Message Pump -- bad idea!)
//
Nino2.agsInit+2B21E1 - 77 BD                 - ja Nino2.agsInit+2B21A0

66 0F 2F F8  77 BD  48 2B CA // Limiter Enabled
-----
66 0F 2F F8  90 90  48 2B CA // Limiter Disabled
*/



struct
{
  const char* pattern;
  size_t      pattern_len;

  size_t      rep_size;
  ptrdiff_t   rep_off;

  bool        enabled;
  void*       scanned_addr;

  void scan (void)
  {
    if (! scanned_addr)
    {
      scanned_addr = SK_ScanAlignedEx (pattern, pattern_len, nullptr);
    }
  }

  void enable (void)
  {
    scan ();

    if (scanned_addr != nullptr && (! enabled))
    {
      DWORD dwProtect;

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, PAGE_EXECUTE_READWRITE, &dwProtect);

      if (rep_size == 8)
      {
        InterlockedExchange64 ((volatile LONG64*)((intptr_t)scanned_addr + rep_off), *(LONG64 *)pattern);
      }

      else if (rep_size == 2)
      {
        InterlockedExchange16 ((volatile SHORT*)((intptr_t)scanned_addr + rep_off), *(SHORT *)"\x77\xBD");
      }

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, dwProtect, &dwProtect);

      enabled = true;
    }
  }

  void disable (void)
  {
    scan ();

    if (scanned_addr != nullptr && enabled)
    {
      DWORD dwProtect;

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, PAGE_EXECUTE_READWRITE, &dwProtect);

      if (rep_size == 8)
      {
        InterlockedExchange64 ((volatile LONG64*)((intptr_t)scanned_addr + rep_off), *(LONG64 *)"\x90\x90\x90\x90\x90\x90\x90\x90");
      }

      else if (rep_size == 2)
      {
        InterlockedExchange16 ((volatile SHORT*)((intptr_t)scanned_addr + rep_off), *(SHORT *)"\x90\x90");
      }

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, dwProtect, &dwProtect);

      enabled = false;
    }
  }
} instn__write_dt_input  { "\xF3\x0F\x11\x35\x7F\xD9\x85\x00",     8,  8, 0,  true,  nullptr },
  instn__write_dt_render { "\xF3\x0F\x11\x3D\x73\xD9\x85\x00",     8,  8, 0,  true,  nullptr },
  instn__limit_branch    { "\x66\x0F\x2F\xF8\x77\xBD\x48\x2B\xCA", 9,  2, 4,  true,  nullptr };



sk::ParameterFloat* SK_NNK2_VirtualFPS = nullptr;

static float SK_NNK2_target_fps = 0.0f;


#define NNS_VERSION_NUM L"0.0.2"
#define NNS_VERSION_STR L"NiNoStutter v " NNS_VERSION_NUM


unsigned int
__stdcall
SK_NNK2_CheckVersion (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  extern bool
  __stdcall
  SK_FetchVersionInfo (const wchar_t* wszProduct);
  
  extern HRESULT
  __stdcall
  SK_UpdateSoftware (const wchar_t* wszProduct);


  if (SK_FetchVersionInfo (L"NiNoStutter"))
    SK_UpdateSoftware (L"NiNoStutter");

  return 0;
}


void
SK_NNK2_InitPlugin (void)
{
  SK_SetPluginName (NNS_VERSION_STR);

  SK_NNK2_CheckVersion (nullptr);

  SK_NNK2_VirtualFPS =
    dynamic_cast <sk::ParameterFloat *> (
      g_ParameterFactory.create_parameter <float> (L"Fixed-Tick Timestep")
    );

  SK_NNK2_VirtualFPS->register_to_ini (
    dll_ini, L"NiNoStutter.Framerate", L"VirtualFPS"
  );

  SK_NNK2_VirtualFPS->load (SK_NNK2_target_fps);


  if (SK_NNK2_target_fps != 0.0f)
  {
    config.window.background_render = true;

    instn__write_dt_input.disable  ();
    instn__write_dt_render.disable ();
    instn__limit_branch.disable    ();

    static float* pfdTr = (float *)((uint8_t *)GetModuleHandle (nullptr) + 0x11DA454);
    static float* pfdTi = (float *)((uint8_t *)GetModuleHandle (nullptr) + 0x11DA450);

    *pfdTr = ( 1.0f / SK_NNK2_target_fps );
    *pfdTi = ( 1.0f / SK_NNK2_target_fps );
  }
}

bool
SK_NNK2_PlugInCfg (void)
{
  static float* pfdTr = (float *)((uint8_t *)GetModuleHandle (nullptr) + 0x11DA454);
  static float* pfdTi = (float *)((uint8_t *)GetModuleHandle (nullptr) + 0x11DA450);

  if (ImGui::CollapsingHeader (u8"Ni no Kuni™ II Revenant Kingdom", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TreePush ("");

    static bool fixed_timestep = SK_NNK2_target_fps != 0.0f;

    if (ImGui::Checkbox ("Fixed-Timestep", &fixed_timestep))
    {
      if (fixed_timestep)
      {
        extern float target_fps;

        if (SK_NNK2_target_fps == 0.0f)
          SK_NNK2_target_fps = ( target_fps == 0.0f ? 60.0f : std::fabs (target_fps) );

        config.window.background_render = true;

        instn__write_dt_input.disable  ();
        instn__write_dt_render.disable ();
        instn__limit_branch.disable    ();

        *pfdTr = ( 1.0f / SK_NNK2_target_fps );
        *pfdTi = ( 1.0f / SK_NNK2_target_fps );

        SK_NNK2_VirtualFPS->store (SK_NNK2_target_fps);
      }

      else
      {
        instn__write_dt_input.enable  ();
        instn__write_dt_render.enable ();
        instn__limit_branch.enable    ();

        SK_NNK2_target_fps = 0.0f;
        SK_NNK2_VirtualFPS->store (SK_NNK2_target_fps);
      }
    }

    if (fixed_timestep)
    {
      ImGui::SameLine ();

      if (ImGui::InputFloat ("Virtual FPS", &SK_NNK2_target_fps, 0.5f, 5.0f))
      {
        if (SK_NNK2_target_fps < 1.0f) SK_NNK2_target_fps = 1.0f;

        *pfdTr = ( 1.0f / SK_NNK2_target_fps );
        *pfdTi = ( 1.0f / SK_NNK2_target_fps );

        SK_NNK2_VirtualFPS->store (SK_NNK2_target_fps);
      }

      if (ImGui::IsItemHovered ())
        ImGui::SetTooltip ("The framerate to base timing off of -- rendering slower than this rate will cause slow-mo");
    }

    ImGui::Checkbox ("Prevent Controller Disconnect Messages",        &config.input.gamepad.xinput.placehold [0]);
    ImGui::SameLine ();
    ImGui::Checkbox ("Continue Rendering if Game Window is Inactive", &config.window.background_render);

    ImGui::TreePop  ();

    return false;
  }

  return true;
}