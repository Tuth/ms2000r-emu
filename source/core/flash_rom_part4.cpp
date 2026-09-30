void FlashROM::handleProgram(uint32_t addr, uint16_t value) {
    uint8_t data_byte = value & 0xFF;
    if (programPageCount_ < PAGE_SIZE) {
        uint32_t page_offset = programPageCount_;
        uint32_t flash_addr = programPageAddr_ + page_offset;
        if (flash_addr < FLASH_SIZE) {
            data_[flash_addr] &= data_byte;
        }
        programPageCount_++;
    }
    if (programPageCount_ >= PAGE_SIZE) {
        state_ = FlashState::IDLE;
    }
}

void FlashROM::handleReadStatus(uint32_t addr, uint16_t value) {
    uint8_t cmd = value & 0xFF;
    if (cmd == static_cast<uint8_t>(FlashCmd::RESET)) {
        state_ = FlashState::IDLE;
    } else if (cmd == static_cast<uint8_t>(FlashCmd::CLEAR_STATUS)) {
        handleClearStatus();
    }
}

void FlashROM::handleReadId(uint32_t addr, uint16_t value) {
    uint8_t cmd = value & 0xFF;
    if (cmd == static_cast<uint8_t>(FlashCmd::RESET)) {
        state_ = FlashState::IDLE;
        inIdMode_ = false;
    }
}

void FlashROM::handleClearStatus() {
    state_ = FlashState::IDLE;
}

} // namespace MS2000