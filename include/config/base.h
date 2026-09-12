#include <Arduino.h>
#include <Preferences.h>

#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

namespace Config {
    struct Base {
        virtual ~Base();
        virtual void load() = 0;
        virtual void save() const = 0;
        virtual bool isValid() const = 0;
        void printDetails() const;

        protected:
            typedef void (*ValuePrinter)(const Config::Base &config, const char *key);

            const char *domain;
            mutable Preferences preferences;

            explicit Base(const char *domain);
            virtual void printKeyValuePairs() const = 0;
            
            template <typename V>
            void printKeyValuePair(const char *key, const V &value) const {
                printCustomKeyValuePair(key, [&]() {
                    Serial.print(value);
                });
            }

            template <typename F>
            void printCustomKeyValuePair(const char *key, F valuePrinter) const {
                Serial.print(key);
                for (size_t i = strlen(key); i < 15; i++) {
                    Serial.print(" ");
                }
                Serial.print(" = ");
                valuePrinter();
                Serial.println();
            }
    };
}

#endif