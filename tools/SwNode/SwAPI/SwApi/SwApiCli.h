#pragma once

#include "SwMap.h"
#include "SwString.h"

class SwApiCli {
public:
    SwApiCli(int argc, char** argv);

    const SwString& exe() const { return exe_; }
    const SwStringList& positionals() const { return positionals_; }

    bool hasFlag(const SwString& name) const;
    SwString value(const SwString& name, const SwString& defaultValue = SwString()) const;
    int intValue(const SwString& name, int defaultValue) const;

private:
    void parse_(int argc, char** argv);
    static bool startsWith_(const SwString& s, const SwString& prefix);
    static bool isValueOption_(const SwString& key);

    SwString exe_;
    SwStringList positionals_;
    SwMap<SwString, SwString> options_;
    SwMap<SwString, bool> flags_;
};
