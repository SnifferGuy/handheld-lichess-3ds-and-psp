#pragma once
#include <cerrno>
#include <cstdio>
#include <string>

namespace handheld {
enum class CredentialStatus { Missing, Unreadable, Invalid, Present };

inline CredentialStatus readCredential(const char* path,std::string& token) {
    token.clear();
    if (!path) return CredentialStatus::Missing;
    auto* file=std::fopen(path,"rb");
    if (!file) return errno==ENOENT?CredentialStatus::Missing:CredentialStatus::Unreadable;
    char buffer[2049];
    const auto size=std::fread(buffer,1,sizeof(buffer),file);
    const bool failed=std::ferror(file)!=0;
    std::fclose(file);
    if (failed) return CredentialStatus::Unreadable;
    if (!size || size==sizeof(buffer)) return CredentialStatus::Invalid;
    for (size_t n=0;n<size;++n) {
        const char ch=buffer[n];
        if (!((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') ||
              (ch>='0' && ch<='9') || ch=='_')) return CredentialStatus::Invalid;
    }
    token.assign(buffer,size);
    return CredentialStatus::Present;
}
}
