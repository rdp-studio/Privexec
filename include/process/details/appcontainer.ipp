// appcontainer
#ifndef PRIVEXEC_APPCONTAINER_IPP
#define PRIVEXEC_APPCONTAINER_IPP
#include <sddl.h>
#include <Userenv.h>
#include <functional>
#include <ShlObj.h>
#include <memory>
//#include <subauth.h>
#include <winternl.h>
#include "pugixml/pugixml.hpp"
#include "processfwd.hpp"

#pragma comment(lib, "Ntdll.lib")

namespace priv {

template <typename T> struct Entry {
  const char *key;
  const T value;

  Entry(const char *t, const T p) : key(t), value(p) {}

  inline bool operator==(const char *okey) const {
    return strcmp(okey, key) == 0;
  }
};

/*
see :
https://docs.microsoft.com/en-us/uwp/schemas/appxpackage/appxmanifestschema/element-capability
internetClient
internetClientServer
privateNetworkClientServer
documentsLibrary
picturesLibrary
videosLibrary
musicLibrary
enterpriseAuthentication
sharedUserCertificates
removableStorage
https://docs.microsoft.com/en-us/windows/desktop/SecAuthZ/well-known-sids
*/

inline bool capabilitiesadd(widcontainer &caps, const char *key) {
  static const Entry<wid_t> capabilitiesList[] = {
      Entry<wid_t>(u8"internetClient", WinCapabilityInternetClientSid),
      Entry<wid_t>(u8"internetClientServer",
                   WinCapabilityInternetClientServerSid),
      Entry<wid_t>(u8"privateNetworkClientServer",
                   WinCapabilityPrivateNetworkClientServerSid),
      Entry<wid_t>(u8"documentsLibrary", WinCapabilityDocumentsLibrarySid),
      Entry<wid_t>(u8"picturesLibrary", WinCapabilityPicturesLibrarySid),
      Entry<wid_t>(u8"videosLibrary", WinCapabilityVideosLibrarySid),
      Entry<wid_t>(u8"musicLibrary", WinCapabilityMusicLibrarySid),
      Entry<wid_t>(u8"enterpriseAuthentication",
                   WinCapabilityEnterpriseAuthenticationSid),
      Entry<wid_t>(u8"sharedUserCertificates",
                   WinCapabilitySharedUserCertificatesSid),
      Entry<wid_t>(u8"removableStorage", WinCapabilityRemovableStorageSid),
      Entry<wid_t>(u8"appointments", WinCapabilityAppointmentsSid),
      Entry<wid_t>(u8"contacts", WinCapabilityContactsSid),
  };
  const auto &e =
      std::find(std::begin(capabilitiesList), std::end(capabilitiesList), key);
  if (e != std::end(capabilitiesList)) {
    caps.push_back(e->value);
  }
  return true;
}

// Lookup Package.Capabilities.rescap:Capability
// Or Package.Capabilities.Capability
inline bool WellKnownFromAppmanifest(const std::wstring &file,
                                     widcontainer &sids) {
  pugi::xml_document doc;
  if (!doc.load_file(file.c_str())) {
    return false;
  }
  auto elem = doc.child("Package").child("Capabilities");
  for (auto it : elem.children("Capability")) {
    capabilitiesadd(sids, it.attribute("Name").as_string());
  }
  for (auto it : elem.children("rescap:Capability")) {
    capabilitiesadd(sids, it.attribute("Name").as_string());
  }
  return true;
}

inline std::wstring u8w(std::string_view str) {
  std::wstring wstr;
  auto N =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
  if (N > 0) {
    wstr.resize(N);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], N);
  }
  return wstr;
}

inline bool WellKnownFromAppmanifestEx(const std::wstring &file,
                                       std::vector<std::wstring> &cans) {
  pugi::xml_document doc;
  if (!doc.load_file(file.c_str())) {
    return false;
  }
  auto elem = doc.child("Package").child("Capabilities");
  for (auto it : elem.children("Capability")) {
    auto n = it.attribute("Name").as_string();
    cans.push_back(u8w(n));
  }
  for (auto it : elem.children("rescap:Capability")) {
    auto n = it.attribute("Name").as_string();
    cans.push_back(u8w(n));
  }
  return true;
}

bool appcontainer::initialize(const wid_t *begin, const wid_t *end) {
  if (!ca.empty()) {
    kmessage.assign(L"capabilities is initialized");
    return false;
  }
  if (begin == nullptr || end == nullptr) {
    kmessage.assign(L"capabilities well known sid is empty");
    return false;
  }
  for (auto it = begin; it != end; it++) {
    PSID psid = HeapAlloc(GetProcessHeap(), 0, SECURITY_MAX_SID_SIZE);
    if (psid == nullptr) {
      kmessage.assign(L"alloc psid failed");
      return false;
    }
    DWORD sidlistsize = SECURITY_MAX_SID_SIZE;
    if (::CreateWellKnownSid(*it, NULL, psid, &sidlistsize) != TRUE) {
      HeapFree(GetProcessHeap(), 0, psid);
      continue;
    }
    if (::IsWellKnownSid(psid, *it) != TRUE) {
      HeapFree(GetProcessHeap(), 0, psid);
      continue;
    }
    SID_AND_ATTRIBUTES attr;
    attr.Sid = psid;
    attr.Attributes = SE_GROUP_ENABLED;
    ca.push_back(attr);
  }
  constexpr const wchar_t *appid = L"Privexec.Core.AppContainer.v2";
  DeleteAppContainerProfile(appid); // ignore error
  if (CreateAppContainerProfile(appid, appid, appid,
                                (ca.empty() ? NULL : ca.data()),
                                (DWORD)ca.size(), &appcontainersid) != S_OK) {
    kmessage.assign(L"CreateAppContainerProfile");
    return false;
  }
  return true;
}
bool appcontainer::initialize() {
  wid_t wslist[] = {
      WinCapabilityInternetClientSid,
      WinCapabilityInternetClientServerSid,
      WinCapabilityPrivateNetworkClientServerSid,
      WinCapabilityPicturesLibrarySid,
      WinCapabilityVideosLibrarySid,
      WinCapabilityMusicLibrarySid,
      WinCapabilityDocumentsLibrarySid,
      WinCapabilitySharedUserCertificatesSid,
      WinCapabilityEnterpriseAuthenticationSid,
      WinCapabilityRemovableStorageSid,
  };
  return initialize(wslist, wslist + _countof(wslist));
}
//
typedef NTSTATUS(NTAPI *PRtlDeriveCapabilitySidsFromName)(
    PCUNICODE_STRING CapName, PSID CapabilityGroupSid, PSID CapabilitySid);

bool appcontainer::initialize(const std::vector<std::wstring> &names) {
  //
  auto _RtlDeriveCapabilitySidsFromName =
      (PRtlDeriveCapabilitySidsFromName)GetProcAddress(
          GetModuleHandle(L"ntdll.dll"), "RtlDeriveCapabilitySidsFromName");
  for (const auto &n : names) {
    UNICODE_STRING capabilityName;
    RtlInitUnicodeString(&capabilityName, n.c_str());
    PSID ntsid = HeapAlloc(GetProcessHeap(), 0, SECURITY_MAX_SID_SIZE);
    PSID capsid = HeapAlloc(GetProcessHeap(), 0, SECURITY_MAX_SID_SIZE);
    if (_RtlDeriveCapabilitySidsFromName(&capabilityName, ntsid, capsid) != 0) {
      HeapFree(GetProcessHeap(), 0, ntsid);
      HeapFree(GetProcessHeap(), 0, capsid);
      auto err = error_code::lasterror();
      wprintf(L"Add %s error %s\n", n.c_str(), err.message.c_str());
      continue;
    }
    HeapFree(GetProcessHeap(), 0, ntsid);
    SID_AND_ATTRIBUTES attr;
    attr.Sid = capsid;
    attr.Attributes = SE_GROUP_ENABLED;
    ca.push_back(attr);
  }
  constexpr const wchar_t *appid = L"Privexec.Core.AppContainer.v3";
  DeleteAppContainerProfile(appid); // ignore error
  if (CreateAppContainerProfile(appid, appid, appid,
                                (ca.empty() ? NULL : ca.data()),
                                (DWORD)ca.size(), &appcontainersid) != S_OK) {
    kmessage.assign(L"CreateAppContainerProfile");
    return false;
  }
  return true;
}

bool appcontainer::initialize(const std::wstring &appxml) {
  std::vector<std::wstring> cans;
  if (!WellKnownFromAppmanifestEx(appxml, cans)) {
    return false;
  }
  return initialize(cans);
}

bool appcontainer::initializessid(const std::wstring &ssid) {
  if (ConvertStringSidToSidW(ssid.c_str(), &appcontainersid) != TRUE) {
    return false;
  }
  return true;
}

// PAWapper is
using PAttribute = LPPROC_THREAD_ATTRIBUTE_LIST;
bool appcontainer::execute() {
  PROCESS_INFORMATION pi;
  STARTUPINFOEX siex = {sizeof(STARTUPINFOEX)};
  siex.StartupInfo.cb = sizeof(siex);
  SIZE_T cbAttributeListSize = 0;
  InitializeProcThreadAttributeList(NULL, 3, 0, &cbAttributeListSize);
  siex.lpAttributeList = reinterpret_cast<PAttribute>(
      HeapAlloc(GetProcessHeap(), 0, cbAttributeListSize));
  auto act = finally([&] {
    // delete when func exit.
    DeleteProcThreadAttributeList(siex.lpAttributeList);
  });
  if (InitializeProcThreadAttributeList(siex.lpAttributeList, 3, 0,
                                        &cbAttributeListSize) != TRUE) {
    kmessage.assign(L"InitializeProcThreadAttributeList");
    return false;
  }
  SECURITY_CAPABILITIES sc;
  sc.AppContainerSid = appcontainersid;
  sc.Capabilities = (ca.empty() ? NULL : ca.data());
  sc.CapabilityCount = static_cast<DWORD>(ca.size());
  sc.Reserved = 0;
  if (UpdateProcThreadAttribute(siex.lpAttributeList, 0,
                                PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                &sc, sizeof(sc), NULL, NULL) != TRUE) {
    kmessage.assign(L"UpdateProcThreadAttribute");
    return false;
  }
  DWORD createflags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
  if (nc) {
    createflags |= CREATE_NEW_CONSOLE;
  }
  if (CreateProcessW(nullptr, &cmd_[0], nullptr, nullptr, FALSE, createflags,
                     nullptr, Castwstr(cwd_),
                     reinterpret_cast<STARTUPINFOW *>(&siex), &pi) != TRUE) {
    kmessage.assign(L"CreateProcessW");
    return false;
  }
  pid_ = pi.dwProcessId;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
} // namespace priv

#endif