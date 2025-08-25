#include "AbstractConnection.h"

AbstractConnection::AbstractConnection() : lastUsed(std::chrono::steady_clock::now()) {}

void AbstractConnection::markAsUsed() {
    inUse    = true;
    lastUsed = std::chrono::steady_clock::now();
}

void AbstractConnection::markAsUnused() {
    inUse    = false;
    lastUsed = std::chrono::steady_clock::now();
}
