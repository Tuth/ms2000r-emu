uint8_t FlashROM::getStatusRegister() const {
    return buildStatusRegister();
}

SectorInfo FlashROM::getSectorInfo(uint32_t sector) const {
    SectorInfo info;
    if (sector >= NUM_SECTORS) {
        info.start_addr = 0;
        info.end_addr = 0;
        info.protected_ = false;
        return info;
    }
    info.start_addr = sectorStartAddr(sector);
    info.end_addr = sectorEndAddr(sector);
    info.protected_ = sectorProtected_[sector];
    return info;
}

bool FlashROM::eraseSector(uint32_t sector) {
    if (sector >= NUM_SECTORS) return false;
    if (sectorProtected_[sector]) return false;
    uint32_t start = sectorStartAddr(sector);
    uint32_t end = sectorEndAddr(sector);
    std::fill(data_.begin() + start, data_.begin() + end + 1, 0xFF);
    return true;
}

bool FlashROM::eraseChip() {
    for (uint32_t s = 0; s < NUM_SECTORS; ++s) {
        if (!sectorProtected_[s]) {
            uint32_t start = sectorStartAddr(s);
            uint32_t end = sectorEndAddr(s);
            std::fill(data_.begin() + start, data_.begin() + end + 1, 0xFF);
        }
    }
    return true;
}

void FlashROM::programPage(uint32_t addr, const uint8_t* data, uint32_t len) {
    if (addr >= FLASH_SIZE) return;
    uint32_t end_addr = addr + len;
    if (end_addr > FLASH_SIZE) end_addr = FLASH_SIZE;
    for (uint32_t i = addr; i < end_addr; ++i) {
        data_[i] &= data[i - addr];
    }
}

void FlashROM::tick(uint32_t cycles) {
    (void)cycles;
}

bool FlashROM::isSectorErased(uint32_t sector) const {
    if (sector >= NUM_SECTORS) return false;
    uint32_t start = sectorStartAddr(sector);
    uint32_t end = sectorEndAddr(sector);
    for (uint32_t i = start; i <= end; ++i) {
        if (data_[i] != 0xFF) return false;
    }
    return true;
}

void FlashROM::setStatus(uint8_t bits, bool val) {
    (void)bits;
    (void)val;
}

uint8_t FlashROM::buildStatusRegister() const {
    uint8_t status = SR_RDY | SR_RYBY;
    if (state_ == FlashState::ERASING) {
        status &= ~(SR_RDY | SR_RYBY);
    }
    if (state_ == FlashState::PROGRAMMING) {
        status &= ~(SR_RDY | SR_RYBY);
    }
    if (sectorEraseStartCycle_.size() != NUM_SECTORS) {
        status |= SR_ERSERR;
    }
    return status;
}