/*
  This is a library written for the ST25DV64KC Dynamic RFID Tag.
  SparkFun sells these at its website:
  https://www.sparkfun.com/products/

  Do you like this library? Help support open source hardware. Buy a board!

  Written by Ricardo Ramos  @ SparkFun Electronics, January 6th, 2021
  This file implements all functions used in the ST25DV64KC Dynamic RFID Tag Arduino Library.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "SparkFun_ST25DV64KC_Arduino_Library.h"
#include "SparkFun_ST25DV64KC_Arduino_Library_Constants.h"

bool SFE_ST25DV64KC::begin(TwoWire &i2cPort)
{
  st25_io.begin(i2cPort);
  return isConnected();
}

void SFE_ST25DV64KC::setErrorCallback(void (*errorCallback)(SF_ST25DV64KC_ERROR errorCode))
{
  _errorCallback = errorCallback;
}

const char *SFE_ST25DV64KC::errorCodeString(SF_ST25DV64KC_ERROR errorCode)
{
  switch (errorCode)
  {
  case SF_ST25DV64KC_ERROR::NONE:
    return "NONE";
    break;
  case SF_ST25DV64KC_ERROR::I2C_INITIALIZATION_ERROR:
    return "I2C_INITIALIZATION_ERROR";
    break;
  case SF_ST25DV64KC_ERROR::INVALID_DEVICE:
    return "INVALID_DEVICE";
    break;
  case SF_ST25DV64KC_ERROR::I2C_SESSION_NOT_OPENED:
    return "I2C_SESSION_NOT_OPENED";
    break;
  case SF_ST25DV64KC_ERROR::I2CSS_MEMORY_AREA_INVALID:
    return "I2CSS_MEMORY_AREA_INVALID";
    break;
  case SF_ST25DV64KC_ERROR::INVALID_WATCHDOG_VALUE:
    return "INVALID_WATCHDOG_VALUE";
    break;
  case SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED:
    return "INVALID_MEMORY_AREA_PASSED";
    break;
  case SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_SIZE:
    return "INVALID_MEMORY_AREA_SIZE";
    break;
  case SF_ST25DV64KC_ERROR::OUT_OF_MEMORY:
    return "OUT_OF_MEMORY";
    break;
  case SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR:
    return "I2C_TRANSMISSION_ERROR";
    break;
  default:
    return "UNDEFINED";
    break;
  }
};

bool SFE_ST25DV64KC::writeNDEFURI(const char *uri, uint8_t idCode, uint16_t *address, bool MB, bool ME)
{
  // Total length could be: strlen(uri) + 8 (see above) + 2 (if L field > 0xFE) + 3 (if PAYLOAD LENGTH > 255)
  uint8_t *tagWrite = new uint8_t[strlen(uri) + 13];

  if (tagWrite == NULL)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::OUT_OF_MEMORY);
    return false; // Memory allocation failed
  }

  memset(tagWrite, 0, strlen(uri) + 13);

  uint8_t *tagPtr = tagWrite;

  uint16_t payloadLength = strlen(uri) + 1; // Payload length is strlen(uri) + Record Type

  // Total field length is strlen(uri) + Prefix Code + Record Type + Payload Length + Type Length + Record Header
  uint16_t fieldLength = strlen(uri) + 1 + 1 + ((payloadLength <= 0xFF) ? 1 : 4) + 1 + 1;

  // Only write the Type 5 T & L fields if the Message Begin bit is set
  if (MB)
  {
    *tagPtr++ = SFE_ST25DV_TYPE5_NDEF_MESSAGE_TLV; // Type5 Tag TLV-Format: T (Type field)

    if (fieldLength > 0xFE) // Is the total L greater than 0xFE?
    {
      *tagPtr++ = 0xFF; // Type5 Tag TLV-Format: L (Length field) (3-Byte Format)
      *tagPtr++ = fieldLength >> 8;
      *tagPtr++ = fieldLength & 0xFF;
    }
    else
    {
      *tagPtr++ = fieldLength; // Type5 Tag TLV-Format: L (Length field) (1-Byte Format)
    }
  }

  // NDEF Record Header
  *tagPtr++ = (MB ? SFE_ST25DV_NDEF_MB : 0) | (ME ? SFE_ST25DV_NDEF_ME : 0) | ((payloadLength <= 0xFF) ? SFE_ST25DV_NDEF_SR : 0) | SFE_ST25DV_NDEF_TNF_WELL_KNOWN;
  *tagPtr++ = 0x01; // NDEF Type Length
  if (payloadLength <= 0xFF)
  {
    *tagPtr++ = payloadLength; // NDEF Payload Length (1-Byte)
  }
  else
  {
    *tagPtr++ = 0; //payloadLength >> 24; // NDEF Payload Length (4-Byte)
    *tagPtr++ = 0; //(payloadLength >> 16) & 0xFF;
    *tagPtr++ = (payloadLength >> 8) & 0xFF;
    *tagPtr++ = payloadLength & 0xFF;
  }
  *tagPtr++ = SFE_ST25DV_NDEF_URI_RECORD; // NDEF Record Type
  *tagPtr++ = idCode; // NDEF URI Prefix Code

  strcpy((char *)tagPtr, uri); // Add the URI

  tagPtr += strlen(uri);

  if (ME)
  {
    *tagPtr++ = SFE_ST25DV_TYPE5_TERMINATOR_TLV; // Type5 Tag TLV-Format: T (Type field)
  }

  uint16_t memLoc = _ccFileLen; // Write to this memory location
  uint16_t numBytes = tagPtr - tagWrite;

  if (address != NULL)
  {
    memLoc = *address;
  }

  bool result = writeEEPROM(memLoc, tagWrite, numBytes);

  if ((address != NULL) && (result))
  {
    *address = memLoc + numBytes - (ME ? 1 : 0); // Update address so the next writeNDEFURI can append to this one
  }

  // If Message Begin is not set, we need to go back and update the L field
  if (!MB)
  {
    uint16_t baseAddress = _ccFileLen + 1; // Skip the SFE_ST25DV_TYPE5_NDEF_MESSAGE_TLV
    uint8_t data[3];
    result &= readEEPROM(baseAddress, data, 0x03); // Read the possible three length bytes
    if (!result)
      return false;
    if (data[0] == 0xFF) // Is the length already 3-byte?
    {
      uint16_t oldLen = ((uint16_t)data[1] << 8) | data[2];
      oldLen += (ME ? numBytes - 1 : numBytes);
      data[1] = oldLen >> 8;
      data[2] = oldLen & 0xFF;
      result &= writeEEPROM(baseAddress, data, 0x03); // Update the existing 3-byte length
    }
    else
    {
      // Length is 1-byte
      uint16_t newLen = data[0];
      newLen += (ME ? numBytes - 1 : numBytes);
      if (newLen <= 0xFE) // Is the new length still 1-byte?
      {
        data[0] = newLen;
        result &= writeEEPROM(baseAddress, data, 0x01); // Update the existing 1-byte length
      }
      else
      {
        // The length was 1-byte but needs to be changed to 3-byte
        delete[] tagWrite; // Resize tagWrite
        tagWrite = new uint8_t[newLen + 4];
        if (tagWrite == NULL)
        {
          SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::OUT_OF_MEMORY);
          return false; // Memory allocation failed
        }
        tagPtr = tagWrite; // Reset tagPtr

        *tagPtr++ = 0xFF; // Change length to 3-byte
        *tagPtr++ = newLen >> 8;
        *tagPtr++ = newLen & 0xFF;
        result &= readEEPROM(baseAddress + 1, tagPtr, (ME ? newLen + 1 : newLen)); // Copy in the old data
        if (!result)
        {
          delete[] tagWrite;
          return false;
        }
        result &= writeEEPROM(baseAddress, tagWrite, (ME ? newLen + 4 : newLen + 3));
        if (result)
          *address = *address + 2; // Update address too
      }
    }
  }

  delete[] tagWrite; // Release the memory

  return result;
}

bool SFE_ST25DV64KC::isConnected()
{
  bool connected = st25_io.isConnected();
  return connected;
}

bool SFE_ST25DV64KC::readRegisterValue(const SF_ST25DV64KC_ADDRESS addressType, const uint16_t registerAddress, uint8_t *value)
{
  bool success = st25_io.readSingleByte(addressType, registerAddress, value);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::readRegisterValues(const SF_ST25DV64KC_ADDRESS addressType, const uint16_t registerAddress, uint8_t *data, uint16_t dataLength)
{
  bool success = st25_io.readMultipleBytes(addressType, registerAddress, data, dataLength);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getDeviceUID(uint8_t *values)
{
  uint8_t tempBuffer[8] = {0};

  // Get UID into tempBuffer and return it from back to front
  bool success = st25_io.readMultipleBytes(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_UID_BASE, tempBuffer, 8);

  if (success)
  {
    for (uint8_t i = 0; i < 8; i++)
      values[i] = tempBuffer[7 - i];
  }

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getDeviceRevision(uint8_t *value)
{
  bool success =  st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_IC_REV, value);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::openI2CSession(uint8_t *password)
{
  // Passwords are written MSB first and need to be sent twice with 0x09 sent after the first
  // set of 8 bytes.
  uint8_t tempBuffer[17] = {0};

  // First 8 bytes
  for (uint8_t i = 0; i < 8; i++)
    tempBuffer[i] = password[7 - i];

  // 9th byte - verification code
  tempBuffer[8] = 0x09;

  // remaining 8 bytes
  for (uint8_t i = 0; i < 8; i++)
    tempBuffer[i + 9] = tempBuffer[i];

  bool success = st25_io.writeMultipleBytes(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2C_PASSWD_BASE, tempBuffer, 17);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::isI2CSessionOpen()
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::DATA, REG_I2C_SSO_DYN, BIT_I2C_SSO_DYN_I2C_SSO);
}

bool SFE_ST25DV64KC::writeI2CPassword(uint8_t *password)
{
  if (!isI2CSessionOpen())
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_SESSION_NOT_OPENED);
    return false;
  }

  // Disable Fast Transfer Mode (datasheet page 75)
  bool ftmIsSet = st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_FTM, BIT_FTM_MB_MODE);
  
  bool success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_FTM, BIT_FTM_MB_MODE);

  // Passwords are written MSB first and need to be sent twice with 0x07 sent after the first
  // set of 8 bytes.
  uint8_t tempBuffer[17] = {0};

  // First 8 bytes
  for (uint8_t i = 0; i < 8; i++)
    tempBuffer[i] = password[7 - i];

  // 9th byte - verification code
  tempBuffer[8] = 0x07;

  // remaining 8 bytes
  for (uint8_t i = 0; i < 8; i++)
    tempBuffer[i + 9] = tempBuffer[i];

  success &= st25_io.writeMultipleBytes(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2C_PASSWD_BASE, tempBuffer, 17);

  if (ftmIsSet)
    success &= st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_FTM, BIT_FTM_MB_MODE);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::programEEPROMReadProtectionBit(uint8_t memoryArea, bool readSecured)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2CSS_MEMORY_AREA_INVALID);
    return false;
  }
  
  bool success = false;

  switch (memoryArea)
  {
  case 1:
  {
    if (readSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_READ);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_READ);
  }
  break;

  case 2:
  {
    if (readSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_READ);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_READ);
  }
  break;

  case 3:
  {
    if (readSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_READ);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_READ);
  }
  break;

  case 4:
  {
    if (readSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_READ);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_READ);
  }
  break;

  default:
    break;
  }

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::programEEPROMWriteProtectionBit(uint8_t memoryArea, bool writeSecured)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2CSS_MEMORY_AREA_INVALID);
    return false;
  }

  bool success = false;

  switch (memoryArea)
  {
  case 1:
  {
    if (writeSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_WRITE);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_WRITE);
  }
  break;

  case 2:
  {
    if (writeSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_WRITE);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_WRITE);
  }
  break;

  case 3:
  {
    if (writeSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_WRITE);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_WRITE);
  }
  break;

  case 4:
  {
    if (writeSecured)
      success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_WRITE);
    else
      success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_WRITE);
  }
  break;

  default:
    break;
  }

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getEEPROMReadProtectionBit(uint8_t memoryArea)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2CSS_MEMORY_AREA_INVALID);
    return false;
  }

  switch (memoryArea)
  {
  case 1:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_READ);
  }
  break;

  case 2:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_READ);
  }
  break;

  case 3:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_READ);
  }
  break;

  case 4:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_READ);
  }
  break;

  default:
    break;
  }
  return false;
}

bool SFE_ST25DV64KC::getEEPROMWriteProtectionBit(uint8_t memoryArea)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2CSS_MEMORY_AREA_INVALID);
    return false;
  }

  switch (memoryArea)
  {
  case 1:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM1_WRITE);
  }
  break;

  case 2:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM2_WRITE);
  }
  break;

  case 3:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM3_WRITE);
  }
  break;

  case 4:
  {
    return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_I2CSS, BIT_I2CSS_MEM4_WRITE);
  }
  break;

  default:
    break;
  }
  return false;
}

bool SFE_ST25DV64KC::writeEEPROM(uint16_t baseAddress, uint8_t *data, uint16_t dataLength)
{
  // Disable FTM temporarily if enabled
  bool ftmEnabled = st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::DATA, REG_MB_CTRL_DYN, BIT_FTM_MB_MODE);

  bool success = true;

  if (ftmEnabled)
    success &= st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_FTM, BIT_FTM_MB_MODE);

  success &= st25_io.writeMultipleBytes(SF_ST25DV64KC_ADDRESS::DATA, baseAddress, data, dataLength);

  // Restore FTM if previously enabled
  if (ftmEnabled)
    success &= st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_FTM, BIT_FTM_MB_MODE);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::readEEPROM(uint16_t baseAddress, uint8_t *data, uint16_t dataLength)
{
  bool success =  st25_io.readMultipleBytes(SF_ST25DV64KC_ADDRESS::DATA, baseAddress, data, dataLength);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return (success);
}

bool SFE_ST25DV64KC::setMemoryAreaEndAddress(uint8_t memoryArea, uint8_t endAddressValue)
{
  if (memoryArea < 1 || memoryArea > 3)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return false;
  }

  bool success = false;

  switch (memoryArea)
  {
  case 1:
    success = st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA1, endAddressValue);
    break;

  case 2:
    success = st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA2, endAddressValue);
    break;

  case 3:
    success = st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA3, endAddressValue);
    break;

  default:
    break;
  }

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
    return false;
  }

  return success;
}

uint16_t SFE_ST25DV64KC::getMemoryAreaEndAddress(uint8_t memoryArea)
{
  if (memoryArea < 1 || memoryArea > 3)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return 0;
  }

  uint8_t value = 0;
  bool result = false;

  switch (memoryArea)
  {
  case 1:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA1, &value);
    break;

  case 2:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA2, &value);
    break;

  case 3:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_ENDA3, &value);
    break;

  default:
    break;
  }

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
    return 0;
  }

  return ((uint16_t)value * 32 + 31);
}

bool SFE_ST25DV64KC::setAreaRfRwProtection(uint8_t memoryArea, SF_ST25DV_RF_RW_PROTECTION rw)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return 0;
  }

  uint8_t value = 0;
  bool result = false;

  switch (memoryArea)
  {
  case 1:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, &value);
    break;

  case 2:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, &value);
    break;

  case 3:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, &value);
    break;

  case 4:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA4SS, &value);
    break;

  default:
    break;
  }

  value &= ~0x0C; // Clear the two RW bits
  value |= ((uint8_t)rw) << 2; // Or in the new RW bits

  switch (memoryArea)
  {
  case 1:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, value);
    break;

  case 2:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, value);
    break;

  case 3:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, value);
    break;

  case 4:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA4SS, value);
    break;

  default:
    break;
  }

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return result;
}

SF_ST25DV_RF_RW_PROTECTION SFE_ST25DV64KC::getAreaRfRwProtection(uint8_t memoryArea)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return SF_ST25DV_RF_RW_PROTECTION::RF_RW_READ_ALWAYS_WRITE_ALWAYS; // Return the default
  }

  uint8_t value = 0;
  bool result = false;

  switch (memoryArea)
  {
  case 1:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, &value);
    break;

  case 2:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, &value);
    break;

  case 3:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, &value);
    break;

  default:
    break;
  }

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
    return SF_ST25DV_RF_RW_PROTECTION::RF_RW_READ_ALWAYS_WRITE_ALWAYS; // Return the default
  }

  return ((SF_ST25DV_RF_RW_PROTECTION)((value >> 2) & 0x03));
}

bool SFE_ST25DV64KC::setAreaRfPwdCtrl(uint8_t memoryArea, SF_ST25DV_RF_PWD_CTRL pwdCtrl)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return 0;
  }

  uint8_t value = 0;
  bool result = false;

  switch (memoryArea)
  {
  case 1:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, &value);
    break;

  case 2:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, &value);
    break;

  case 3:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, &value);
    break;

  case 4:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA4SS, &value);
    break;

  default:
    break;
  }

  value &= ~0x03; // Clear the two pwd ctrl bits
  value |= (uint8_t)pwdCtrl; // Or in the new pwd ctrl bits

  switch (memoryArea)
  {
  case 1:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, value);
    break;

  case 2:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, value);
    break;

  case 3:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, value);
    break;

  case 4:
    result &= st25_io.writeSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA4SS, value);
    break;

  default:
    break;
  }

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return result;
}

SF_ST25DV_RF_PWD_CTRL SFE_ST25DV64KC::getAreaRfPwdCtrl(uint8_t memoryArea)
{
  if (memoryArea < 1 || memoryArea > 4)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::INVALID_MEMORY_AREA_PASSED);
    return SF_ST25DV_RF_PWD_CTRL::RF_PWD_NEVER; // Return the default
  }

  uint8_t value = 0;
  bool result = false;

  switch (memoryArea)
  {
  case 1:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA1SS, &value);
    break;

  case 2:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA2SS, &value);
    break;

  case 3:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA3SS, &value);
    break;

  case 4:
    result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_RFA4SS, &value);
    break;

  default:
    break;
  }

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
    return SF_ST25DV_RF_PWD_CTRL::RF_PWD_NEVER; // Return the default
  }

  return ((SF_ST25DV_RF_PWD_CTRL)(value & 0x03));
}

bool SFE_ST25DV64KC::RFFieldDetected()
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_EH_CTRL_DYN, BIT_EH_CTRL_DYN_FIELD_ON);
}

bool SFE_ST25DV64KC::setGPO1Bit(uint8_t bitMask, bool enabled)
{
  bool success;

  if (enabled)
    success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO1, bitMask);
  else
    success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO1, bitMask);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getGPO1Bit(uint8_t bitMask)
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO1, bitMask);
}

bool SFE_ST25DV64KC::setGPO2Bit(uint8_t bitMask, bool enabled)
{
  bool success;

  if (enabled)
    success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO2, bitMask);
  else
    success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO2, bitMask);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getGPO2Bit(uint8_t bitMask)
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_GPO2, bitMask);
}

bool SFE_ST25DV64KC::setGPO_CTRL_DynBit(bool enabled)
{
  bool success;

  if (enabled)
    success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_GPO_CTRL_DYN, BIT_GPO_CTRL_DYN_GPO_EN);
  else
    success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_GPO_CTRL_DYN, BIT_GPO_CTRL_DYN_GPO_EN);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getGPO_CTRL_DynBit()
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_GPO_CTRL_DYN, BIT_GPO_CTRL_DYN_GPO_EN);
}

uint8_t SFE_ST25DV64KC::getIT_STS_Dyn()
{
  uint8_t value = 0;
  
  bool result = st25_io.readSingleByte(SF_ST25DV64KC_ADDRESS::DATA, REG_IT_STS_DYN, &value);

  if (!result)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return value;
}

bool SFE_ST25DV64KC::setEH_MODEBit(bool value)
{
  bool success;

  if (value)
    success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_EH_MODE, BIT_EH_MODE_EH_MODE);
  else
    success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_EH_MODE, BIT_EH_MODE_EH_MODE);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getEH_MODEBit()
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::SYSTEM, REG_EH_MODE, BIT_EH_MODE_EH_MODE);
}

bool SFE_ST25DV64KC::setEH_CTRL_DYNBit(uint8_t bitMask, bool value)
{
  bool success;

  if (value)
    success = st25_io.setRegisterBit(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_EH_CTRL_DYN, bitMask);
  else
    success = st25_io.clearRegisterBit(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_EH_CTRL_DYN, bitMask);

  if (!success)
  {
    SAFE_CALLBACK(_errorCallback, SF_ST25DV64KC_ERROR::I2C_TRANSMISSION_ERROR);
  }

  return success;
}

bool SFE_ST25DV64KC::getEH_CTRL_DYNBit(uint8_t bitMask)
{
  return st25_io.isBitSet(SF_ST25DV64KC_ADDRESS::DATA, DYN_REG_EH_CTRL_DYN, bitMask);
}
