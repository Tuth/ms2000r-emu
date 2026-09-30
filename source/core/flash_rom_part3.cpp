void FlashROM::processCommand(uint32_t addr, uint16_t value) {
    lastAddr_ = addr;
    switch (state_) {
        case FlashState::IDLE:
            handleIdle(addr, value);
            break;
        case FlashState::CMD1:
            handleCmd1(addr, value);
            break;
        case FlashState::CMD2_SECTOR_ERASE:
            handleSectorEraseConfirm(addr, value);
            break;
        case FlashState::CMD2_CHIP_ERASE:
            handleChipEraseConfirm(addr, value);
            break;
        case FlashState::CMD2_PROGRAM:
            handleProgram(addr, value);
            break;
        case FlashState::READ_STATUS_REG:
            handleReadStatus(addr, value);
            break;
        case FlashState::READ_ID_MODE:
            handleReadId(addr, value);
            break;
        case FlashState::PROGRAMMING:
        case FlashState::ERASING:
            break;
    }
}

void FlashROM::handleIdle(uint32_t addr, uint16_t value) {
    cmdBuffer_ = value & 0xFF;
    switch (static_cast<FlashCmd>(cmdBuffer_)) {
        case FlashCmd::RESET:
            state_ = FlashState::IDLE;
            inIdMode_ = false;
            break;
        case FlashCmd::READ_ID:
        case FlashCmd::READ_ID_ALT:
            state_ = FlashState::READ_ID_MODE;
            inIdMode_ = true;
            break;
        case FlashCmd::READ_STATUS:
            state_ = FlashState::READ_STATUS_REG;
            break;
        case FlashCmd::CLEAR_STATUS:
            handleClearStatus();
            break;
        case FlashCmd::SECTOR_ERASE:
            state_ = FlashState::CMD2_SECTOR_ERASE;
            pendingEraseConfirm_ = true;
            break;
        case FlashCmd::CHIP_ERASE:
            state_ = FlashState::CMD2_CHIP_ERASE;
            break;
        case FlashCmd::PAGE_PROGRAM:
        case FlashCmd::PAGE_PROGRAM_ALT:
            state_ = FlashState::CMD2_PROGRAM;
            programPageAddr_ = addr & ~(PAGE_SIZE - 1);
            programPageCount_ = 0;
            break;
        default:
            state_ = FlashState::IDLE;
            break;
    }
}

void FlashROM::handleCmd1(uint32_t addr, uint16_t value) {
    uint8_t cmd = value & 0xFF;
    if (cmd == static_cast<uint8_t>(FlashCmd::RESET)) {
        state_ = FlashState::IDLE;
        inIdMode_ = false;
    }
}

void FlashROM::handleSectorEraseConfirm(uint32_t addr, uint16_t value) {
    uint8_t cmd = value & 0xFF;
    if (cmd == 0xD0) {
        uint32_t sector = getSectorIndex(lastAddr_);
        if (sector < NUM_SECTORS && !sectorProtected_[sector]) {
            state_ = FlashState::ERASING;
            eraseSector(sector);
            state_ = FlashState::IDLE;
        }
    } else if (cmd == static_cast<uint8_t>(FlashCmd::RESET)) {
        state_ = FlashState::IDLE;
    }
    pendingEraseConfirm_ = false;
}

void FlashROM::handleChipEraseConfirm(uint32_t addr, uint16_t value) {
    uint8_t cmd = value & 0xFF;
    if (cmd == 0x10) {
        state_ = FlashState::ERASING;
        eraseChip();
        state_ = FlashState::IDLE;
    } else if (cmd == static_cast<uint8_t>(FlashCmd::RESET)) {
        state_ = FlashState::IDLE;
    }
}