#include "config/base.h"

#include <Preferences.h>
#include "nvs_flash.h"
#include "nvs.h"

Config::Base::Base(const char *domain) : domain(domain) {
    preferences.begin(domain, false);
}

Config::Base::~Base() {
    preferences.end();
}

void Config::Base::printDetails() const {
    printKeyValuePair("domain", domain);
    printKeyValuePairs();
    printKeyValuePair("status", isValid() ? "VALID" : "INVALID");
}
