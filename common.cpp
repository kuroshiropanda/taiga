/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2011, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "std.h"
#include "animelist.h"
#include "third_party/base64/base64.h"
#include "common.h"
#include "myanimelist.h"
#include "settings.h"
#include "string.h"
#include "theme.h"
#include "win32/win_registry.h"
#include "third_party/zlib/zlib.h"

// =============================================================================

wstring Base64Decode(const wstring& str, bool for_filename) {
  if (str.empty()) return L"";
  Base64Coder coder;
  string buff = ToANSI(str);
  coder.Decode((BYTE*)buff.c_str(), buff.length());
  if (for_filename) {
    wstring msg = ToUTF8(coder.DecodedMessage());
    ReplaceChar(msg, '-', '/');
    return msg;
  } else {
    return ToUTF8(coder.DecodedMessage());
  }
}

wstring Base64Encode(const wstring& str, bool for_filename) {
  if (str.empty()) return L"";
  Base64Coder coder;
  string buff = ToANSI(str);
  coder.Encode((BYTE*)buff.c_str(), buff.length());
  if (for_filename) {
    wstring msg = ToUTF8(coder.EncodedMessage());
    ReplaceChar(msg, '/', '-');
    return msg;
  } else {
    return ToUTF8(coder.EncodedMessage());
  }
}

// =============================================================================

wstring CalculateCRC(const wstring& file) {
  BYTE buffer[0x10000];
  DWORD dwBytesRead = 0;
  
  HANDLE hFile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, 0);
  if (hFile == INVALID_HANDLE_VALUE) return L"";

  ULONG crc = crc32(0L, Z_NULL, 0);
  BOOL bSuccess = ReadFile(hFile, buffer, sizeof(buffer), &dwBytesRead, NULL);
  while (bSuccess && dwBytesRead) {
    crc = crc32(crc, buffer, dwBytesRead);
    bSuccess = ReadFile(hFile, buffer, sizeof(buffer), &dwBytesRead, NULL);
  }

  if (hFile != NULL) CloseHandle(hFile);

  wchar_t val[16] = {0};
  _ultow_s(crc, val, 16, 16);
  wstring value = val;
  if (value.length() < 8) {
    value.insert(0, 8 - value.length(), '0');
  }
  return value;
}

// =============================================================================

int GetEpisodeHigh(const wstring& episode_number) {
  int value = 1, pos = InStrRev(episode_number, L"-", episode_number.length());
  if (pos == episode_number.length() - 1) {
    value = ToINT(episode_number.substr(0, pos));
  } else if (pos > -1) {
    value = ToINT(episode_number.substr(pos + 1));
  } else {
    value = ToINT(episode_number);
  }
  return value;
}

int GetEpisodeLow(const wstring& episode_number) {
  return ToINT(episode_number); // ToINT() stops at -
}

void SplitEpisodeNumbers(const wstring& input, vector<int>& output) {
  if (input.empty()) return;
  vector<wstring> numbers;
  Split(input, L"-", numbers);
  for (auto it = numbers.begin(); it != numbers.end(); ++it) {
    output.push_back(ToINT(*it));
  }
}

wstring JoinEpisodeNumbers(const vector<int>& input) {
  wstring output;
  for (auto it = input.begin(); it != input.end(); ++it) {
    if (!output.empty()) output += L"-";
    output += ToWSTR(*it);
  }
  return output;
}

// =============================================================================

int StatusToIcon(int status) {  
  switch (status) {
    case MAL_AIRING:
      return ICON16_GREEN;
    case MAL_FINISHED:
      return ICON16_BLUE;
    case MAL_NOTYETAIRED:
      return ICON16_RED;
    default:
      return ICON16_GRAY;
  }
}

// =============================================================================

wstring FormatError(DWORD dwError, LPCWSTR lpSource) {
  DWORD dwFlags = FORMAT_MESSAGE_IGNORE_INSERTS;
  HMODULE hInstance = NULL;
  const DWORD size = 101;
  WCHAR buffer[size];

  if (lpSource) {
    dwFlags |= FORMAT_MESSAGE_FROM_HMODULE;
    hInstance = LoadLibrary(lpSource);
    if (!hInstance) return L"";
  } else {
    dwFlags |= FORMAT_MESSAGE_FROM_SYSTEM;
  }

  if (FormatMessage(dwFlags, hInstance, dwError, 
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, size, NULL)) {
      if (hInstance) FreeLibrary(hInstance);
      return buffer;
  } else {
    if (hInstance) FreeLibrary(hInstance);
    return ToWSTR(dwError);
  }
}

// =============================================================================

unsigned long GetFileAge(const wstring& path) {
  FILETIME ft_file, ft_now;
  
  // Get the time the file was last modified
  HANDLE hFile = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 
    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return 0;
  BOOL result = GetFileTime(hFile, NULL, NULL, &ft_file);
  CloseHandle(hFile);
  if (!result) return 0;
  
  // Get current time
  SYSTEMTIME st_now;
  GetSystemTime(&st_now);
  SystemTimeToFileTime(&st_now, &ft_now);

  // Convert to ULARGE_INTEGER
  ULARGE_INTEGER ul_file, ul_now;
  ul_file.LowPart = ft_file.dwLowDateTime;
  ul_file.HighPart = ft_file.dwHighDateTime;
  ul_now.LowPart = ft_now.dwLowDateTime;
  ul_now.HighPart = ft_now.dwHighDateTime;

  // Return difference in seconds
  return static_cast<unsigned long>((ul_now.QuadPart - ul_file.QuadPart) / 10000000);
}

void GetSystemTime(SYSTEMTIME& st, int utc_offset) {
  // Get current time, expressed in UTC
  GetSystemTime(&st);
  if (utc_offset == 0) return;
  
  // Convert to FILETIME
  FILETIME ft;
  SystemTimeToFileTime(&st, &ft);
  // Convert to ULARGE_INTEGER
  ULARGE_INTEGER ul;
  ul.LowPart = ft.dwLowDateTime;
  ul.HighPart = ft.dwHighDateTime;

  // Apply UTC offset
  ul.QuadPart += static_cast<ULONGLONG>(utc_offset) * 60 * 60 * 10000000;

  // Convert back to SYSTEMTIME
  ft.dwLowDateTime = ul.LowPart;
  ft.dwHighDateTime = ul.HighPart;
  FileTimeToSystemTime(&ft, &st);
}

wstring GetDate(LPCWSTR lpFormat) {
  WCHAR buff[32];
  GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, lpFormat, buff, 32);
  return buff;
}

wstring GetTime(LPCWSTR lpFormat) {
  WCHAR buff[32];
  GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, lpFormat, buff, 32);
  return buff;
}

wstring GetDateJapan(LPCWSTR lpFormat) {
  WCHAR buff[32];
  SYSTEMTIME stJST;
  GetSystemTime(stJST, 9); // JST is UTC+09
  GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &stJST, lpFormat, buff, 32);
  return buff;
}

wstring GetTimeJapan(LPCWSTR lpFormat) {
  WCHAR buff[32];
  SYSTEMTIME stJST;
  GetSystemTime(stJST, 9); // JST is UTC+09
  GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, &stJST, lpFormat, buff, 32);
  return buff;
}

wstring ToDateString(time_t seconds) {
  time_t days, hours, minutes;
  wstring date;

  if (seconds > 0) {
    #define CALC_TIME(x, y) x = seconds / (y); seconds = seconds % (y);
    CALC_TIME(days, 60 * 60 * 24);
    CALC_TIME(hours, 60 * 60);
    CALC_TIME(minutes, 60);
    #undef CALC_TIME
    date.clear();
    #define ADD_TIME(x, y) \
      if (x > 0) { \
        if (!date.empty()) date += L" "; \
        date += ToWSTR(x) + y; \
        if (x > 1) date += L"s"; \
      }
    ADD_TIME(days, L" day");
    ADD_TIME(hours, L" hour");
    ADD_TIME(minutes, L" minute");
    ADD_TIME(seconds, L" second");
    #undef ADD_TIME
  }

  return date;
}

wstring ToTimeString(int seconds) {
  int hours = seconds / 3600;
  seconds = seconds % 3600;
  int minutes = seconds / 60;
  seconds = seconds % 60;
  #define TWO_DIGIT(x) (x >= 10 ? ToWSTR(x) : L"0" + ToWSTR(x))
  return (hours > 0 ? TWO_DIGIT(hours) + L":" : L"") + 
    TWO_DIGIT(minutes) + L":" + TWO_DIGIT(seconds);
  #undef TWO_DIGIT
}

// =============================================================================

bool Execute(const wstring& path, const wstring& parameters) {
  if (path.empty()) return false;
  HINSTANCE value = ShellExecute(NULL, L"open", path.c_str(), parameters.c_str(), NULL, SW_SHOWNORMAL);
  return reinterpret_cast<int>(value) > 32;
}

BOOL ExecuteEx(const wstring& path, const wstring& parameters) {
  SHELLEXECUTEINFO si = {0};
  si.cbSize = sizeof(SHELLEXECUTEINFO);
  si.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI | SEE_MASK_UNICODE;
  si.lpVerb = L"open";
  si.lpFile = path.c_str();
  si.lpParameters = parameters.c_str();
  si.nShow = SW_SHOWNORMAL;
  return ShellExecuteEx(&si);
}

void ExecuteLink(const wstring& link) {
  ShellExecute(NULL, NULL, link.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

wstring ExpandEnvironmentStrings(const wstring& path) {
  WCHAR buff[MAX_PATH];
  if (::ExpandEnvironmentStrings(path.c_str(), buff, MAX_PATH)) {
    return buff;
  } else {
    return path;
  }
}

wstring BrowseForFile(HWND hwndOwner, LPCWSTR lpstrTitle, LPCWSTR lpstrFilter) {
  WCHAR szFile[MAX_PATH] = {'\0'};

  if (!lpstrFilter) {
    lpstrFilter = L"All files (*.*)\0*.*\0";
  }

  OPENFILENAME ofn = {0};
  ofn.lStructSize  = sizeof(OPENFILENAME);
  ofn.hwndOwner    = hwndOwner;
  ofn.lpstrFile    = szFile;
  ofn.lpstrFilter  = lpstrFilter;
  ofn.lpstrTitle   = lpstrTitle;
  ofn.nMaxFile     = sizeof(szFile);
  ofn.Flags        = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
  
  if (GetOpenFileName(&ofn)) {
    return szFile;
  } else {
    return L"";
  }
}

BOOL BrowseForFolder(HWND hwndOwner, LPCWSTR lpszTitle, UINT ulFlags, wstring& output) {
  BROWSEINFO bi = {0};
  bi.hwndOwner  = hwndOwner;
  bi.lpszTitle  = lpszTitle;
  bi.ulFlags    = ulFlags;
  
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolder(&bi);
  if (pidl == NULL) return FALSE;
  
  WCHAR path[MAX_PATH];
  SHGetPathFromIDList(pidl, path);
  output = path;
  
  if (output.empty()) return FALSE;
  return TRUE;
}

bool CreateFolder(const wstring& path) {
  return SHCreateDirectoryEx(NULL, path.c_str(), NULL) == ERROR_SUCCESS;
}

int DeleteFolder(wstring path) {
  if (path.back() == '\\') path.pop_back();
  path.push_back('\0');
  SHFILEOPSTRUCT fos = {0};
  fos.wFunc = FO_DELETE;
  fos.pFrom = path.c_str();
  fos.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
  return SHFileOperation(&fos);
}

bool FileExists(const wstring& file) {
  if (file.empty()) return false;
  HANDLE hFile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    CloseHandle(hFile);
    return true;
  }
  return false;
}

bool FolderExists(const wstring& path) {
  DWORD file_attr = GetFileAttributes(path.c_str());
  return (file_attr != INVALID_FILE_ATTRIBUTES) && (file_attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool PathExists(const wstring& path) {
  return GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void ValidateFileName(wstring& file) {
  EraseChars(file, L"\\/:*?<>|");
}

wstring GetDefaultAppPath(const wstring& extension, const wstring& default_value) {
  CRegistry reg;
  reg.OpenKey(HKEY_CLASSES_ROOT, extension, 0, KEY_QUERY_VALUE);
  wstring path = reg.QueryValue(L"");
  
  if (!path.empty()) {
    path += L"\\shell\\open\\command";
    reg.OpenKey(HKEY_CLASSES_ROOT, path, 0, KEY_QUERY_VALUE);
    path = reg.QueryValue(L"");
    Replace(path, L"\"", L"");
    Trim(path, L" %1");
  }

  reg.CloseKey();
  return path.empty() ? default_value : path;
}

int PopulateFiles(vector<wstring>& file_list, wstring path, wstring extension, bool recursive, bool trim_extension) {
  if (path.empty()) return 0;
  wstring folder = path + L"*.*";
  int found = 0;

  WIN32_FIND_DATA wfd;
  HANDLE hFind = FindFirstFile(folder.c_str(), &wfd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (recursive && wcscmp(wfd.cFileName, L".") != 0 && wcscmp(wfd.cFileName, L"..") != 0) {
          folder = path + wfd.cFileName + L"\\";
          found += PopulateFiles(file_list, folder, extension, recursive, trim_extension);
        }
      } else if (wfd.dwFileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
        if (extension.empty() || IsEqual(GetFileExtension(wfd.cFileName), extension)) {
          if (trim_extension) {
            file_list.push_back(GetFileWithoutExtension(wfd.cFileName));
          } else {
            file_list.push_back(wfd.cFileName);
          }
          found++;
        }
      }
    } while (FindNextFile(hFind, &wfd));
    FindClose(hFind);
  }

  return found;
}

int PopulateFolders(vector<wstring>& folder_list, wstring path) {
  if (path.empty()) return 0;
  path += L"*.*";
  int found = 0;

  WIN32_FIND_DATA wfd;
  HANDLE hFind = FindFirstFile(path.c_str(), &wfd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (wcscmp(wfd.cFileName, L".") != 0 && wcscmp(wfd.cFileName, L"..") != 0) {
          found++;
          folder_list.push_back(wfd.cFileName);
        }
      }
    } while (FindNextFile(hFind, &wfd));
    FindClose(hFind);
  }

  return found;
}

wstring ToSizeString(QWORD qwSize) {
  wstring size, unit;

  if (qwSize > 1073741824) {      // 2^30
    size = ToWSTR(static_cast<double>(qwSize) / 1073741824, 2);
    unit = L" GB";
  } else if (qwSize > 1048576) {  // 2^20
    size = ToWSTR(static_cast<double>(qwSize) / 1048576, 2);
    unit = L" MB";
  } else if (qwSize > 1024) {     // 2^10
    size = ToWSTR(static_cast<double>(qwSize) / 1024, 2);
    unit = L" KB";
  } else {
    size = ToWSTR(qwSize);
    unit = L" bytes";
  }

  return size + unit;
}