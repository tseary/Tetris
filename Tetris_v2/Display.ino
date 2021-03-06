
#include <LRAS1130.h>

using namespace lr;
AS1130 ledDriver;
AS1130::Current ledCurrent;

typedef AS1130Picture24x5 Picture;
Picture boardPicture; // The picture of the board or general text
Picture textPicture;  // A picture for special text

void initializeDisplay() {
	// Start I2C
	Wire.begin();

	// Wait until the chip is ready.
	delay(100);

	// TODO Check if the chip is addressable
	/*if (!ledDriver.isChipConnected()) {
	  Serial.println(F("Communication problem with chip."));
	  Serial.flush();
	  return;
	}*/

	// Load the brightness from EEPROM
	EEPROM.get(EEPROM_LED_CURRENT, ledCurrent);

	// Sanity check
	if (ledCurrent == 0) {
		ledCurrent = AS1130::Current15mA;
	}

	// Set-up everything
	ledDriver.setRamConfiguration(AS1130::RamConfiguration1);
	ledDriver.setOnOffFrameAllOff(0);
	ledDriver.setBlinkAndPwmSetAll(0);

	// Dim certain rows for multi-colour display
	uint32_t pixelBrightnessRows;
	EEPROM.get(EEPROM_PIXEL_BRIGHTNESS, pixelBrightnessRows);
	const uint8_t SET_INDEX = 0;  // 0 - 5
	for (uint8_t y = 0; y < BOARD_HEIGHT; y++) {
		if (pixelBrightnessRows & ((uint32_t)1 << y)) {
			for (uint8_t x = 0; x < BOARD_WIDTH; x++) {
				ledDriver.setPwmValue(SET_INDEX,
					ledDriver.getLedIndex24x5(BOARD_HEIGHT - 1 - y, x), 0x1C);
			}
		}
	}

	ledDriver.setCurrentSource(ledCurrent); // 0x00 = 0 mA, 0xff = 30 mA
	ledDriver.setScanLimit(AS1130::ScanLimitFull);
	ledDriver.setMovieEndFrame(AS1130::MovieEndWithFirstFrame);
	ledDriver.setMovieFrameCount(4);
	ledDriver.setFrameDelayMs(100);
	ledDriver.setMovieLoopCount(AS1130::MovieLoop6);
	ledDriver.setScrollingEnabled(true);
	ledDriver.startPicture(0);

	// Enable the chip
	ledDriver.startChip();
}

uint8_t getLEDCurrent() {
	return ledCurrent;
}

void setLEDCurrent(uint8_t newCurrent, bool save) {
	ledCurrent = (AS1130::Current)newCurrent;
	ledDriver.setCurrentSource(ledCurrent);
	if (save) {
		EEPROM.put(EEPROM_LED_CURRENT, ledCurrent);
	}
}

// Updates the board display
// drawTetramino determines whether or not to draw the active tetramino
// curtain fills all the pixels up to the given number (if positive),
//   or down to the given number (if negative). Zero means no curtain will be drawn.
void drawBoard() {
	drawBoard(true);
}
void drawBoard(bool drawTetramino) {
	drawBoard(drawTetramino, 0);
}
void drawBoard(bool drawTetramino, int curtain) {
	// Render and output the current state of the board
	for (int boardY = 0; boardY < BOARD_HEIGHT; boardY++) {
		// A row of the image to draw
		uint16_t row;

		// Draw board or curtain
		if (boardY < curtain || boardY >= (BOARD_HEIGHT + curtain)) {
			// Draw curtain
			row = 0x4924 << (boardY % 3);	// 1/3 density to reduce power
		} else {
			// Get a row from the field
			row = field[boardY + BORDER_Y];
		}

		// Draw active piece
		if (drawTetramino) {
			uint16_t tetraminoShape = TETRAMINO_SHAPES[tetraminoType][tetraminoR];
			int tetraminoRowIndex = boardY + BORDER_Y - tetraminoY;
			if (tetraminoRowIndex >= 0 && tetraminoRowIndex < TETRAMINO_SIZE) {
				row |= ((tetraminoShape >> (TETRAMINO_SIZE * tetraminoRowIndex)) &
					TETRAMINO_MASK) << tetraminoX;
			}
		}

		// Draw row
		for (uint16_t rowMask = 1 << (BOARD_WIDTH + BORDER_X - 1), x = 0; (rowMask & ~FIELD_MASK_BORDER) != 0; rowMask >>= 1, x++) {
			boardPicture.setPixel(BOARD_HEIGHT - 1 - boardY, BOARD_WIDTH - 1 - x, row & rowMask);
		}
	}
	ledDriver.setOnOffFrame(0, boardPicture);
}

void drawBlank() {
	ledDriver.setOnOffFrameAllOff(0);
}

/******************************************************************************
* Special Text
******************************************************************************/

const uint8_t scoreText[24] = {
  0b01111,
  0b11000,
  0b00111,
  0b11110,
  0b00000,
  0b01111,
  0b10000,
  0b10000,
  0b01111,
  0b00000,
  0b01110,
  0b10001,
  0b10001,
  0b01110,
  0b00000,
  0b11110,
  0b10001,
  0b11110,
  0b10001,
  0b00000,
  0b11111,
  0b11100,
  0b10000,
  0b11111};

const uint8_t pauseText[24] = {
  0b11110,
  0b10001,
  0b11110,
  0b10000,
  0b00000,
  0b01110,
  0b10001,
  0b11111,
  0b10001,
  0b00000,
  0b10001,
  0b10001,
  0b10001,
  0b01110,
  0b00000,
  0b01111,
  0b11000,
  0b00111,
  0b11110,
  0b00000,
  0b11111,
  0b11100,
  0b10000,
  0b11111};

inline void drawTextScore() {
	drawSpecialText(scoreText);
}
inline void drawTextPause() {
	drawSpecialText(pauseText);
}
void drawSpecialText(const uint8_t* textArray) {
	// Render and output the text array
	for (uint8_t y = 0; y < BOARD_HEIGHT; y++) {
		for (uint8_t x = 0; x < BOARD_WIDTH; x++) {
			textPicture.setPixel(y, x, textArray[y] & (1 << x));
		}
	}
	ledDriver.setOnOffFrame(0, textPicture);
}

/******************************************************************************
 * TEXT
 ******************************************************************************/

const uint8_t letters5High[26][5] = {
  {0b10001, 0b10001, 0b11111, 0b10001, 0b01110},  // A
  {0b11110, 0b10001, 0b11110, 0b10001, 0b11110},  // B
  {0b01111, 0b10000, 0b10000, 0b10000, 0b01111},  // C
  {0b11110, 0b10001, 0b10001, 0b10001, 0b11110},  // D
  {0b11111, 0b10000, 0b11100, 0b10000, 0b11111},  // E

  {0b10000, 0b10000, 0b11100, 0b10000, 0b11111},  // F
  {0b01111, 0b10001, 0b10011, 0b10000, 0b01111},  // G
  {0b10001, 0b10001, 0b11111, 0b10001, 0b10001},  // H
  {0b11111, 0b00100, 0b00100, 0b00100, 0b11111},  // I
  {0b11000, 0b00100, 0b00100, 0b00100, 0b11111},  // J

  {0b10001, 0b10010, 0b11100, 0b10010, 0b10001},  // K
  {0b11111, 0b10000, 0b10000, 0b10000, 0b10000},  // L
  {0b10001, 0b10001, 0b10101, 0b11011, 0b10001},  // M
  {0b10001, 0b10011, 0b10101, 0b11001, 0b10001},  // N
  {0b01110, 0b10001, 0b10001, 0b10001, 0b01110},  // O

  {0b10000, 0b10000, 0b11110, 0b10001, 0b11110},  // P
  {0b01111, 0b10011, 0b10101, 0b10001, 0b01110},  // Q
  {0b10001, 0b10001, 0b11110, 0b10001, 0b11110},  // R
  {0b11110, 0b00001, 0b01110, 0b10000, 0b01111},  // S
  {0b00100, 0b00100, 0b00100, 0b00100, 0b11111},  // T

  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001},  // U
  {0b00100, 0b01010, 0b10001, 0b10001, 0b10001},  // V
  {0b01010, 0b10101, 0b10101, 0b10001, 0b10001},  // W
  {0b10001, 0b01010, 0b00100, 0b01010, 0b10001},  // X
  {0b00100, 0b00100, 0b00100, 0b01010, 0b10001},  // Y

  {0b11111, 0b01000, 0b00100, 0b00010, 0b11111}  // Z
};

void drawText5High(const char* text) {
	for (char i = 0, c = text[i]; i < 4 && c != '\0'; c = text[++i]) {
		// Only draw letters
		if (c < 'A' || c > 'Z') {
			continue;
		}
		uint16_t y = FIELD_HEIGHT - 6 * (i + 1) + 1;
		for (uint8_t r = 0; r < 5; r++) {
			field[y + r] = letters5High[c - 'A'][r] << BORDER_X;
		}
	}
	drawBoard(false);
}

/******************************************************************************
 * Number Text
 ******************************************************************************/

 // 5-wide digits 0 to 5
const uint32_t BOARD_DIGITS_05[5] = {
	0b00111100000111110111111111101110,
	0b00000010000100001100000010010001,
	0b00111101111100110011100010010001,
	0b00100001000100001000011110010001,
	0b00111111000111111111100010001110};

// 5-wide digits 6 to 9
const uint32_t BOARD_DIGITS_69[5] = {
	0b00000000000011110011100010001110,
	0b00000000000000001100010010010001,
	0b00000000000001111011100001011110,
	0b00000000000010001100010000110000,
	0b00000000000001110011101111101111};

void setDisplayDigit5Wide(uint8_t digit, uint8_t x, uint8_t y) {
	for (uint8_t r = 0; r < 5; r++) {
		field[y + r] = (((digit <= 5 ? BOARD_DIGITS_05[r] : BOARD_DIGITS_69[r]) >> (5 * (digit % 6))) & 0b11111) << (BORDER_X + x);
	}
}

void drawNumber(uint16_t score) {
	for (uint8_t i = 0; i == 0 || (i < 4 && score != 0); i++) {
		setDisplayDigit5Wide(score % 10, 0, BORDER_Y + 1 + 6 * i);
		score /= 10;
	}
	drawBoard(false);
}

/******************************************************************************
 * DEBUG
 ******************************************************************************/

void printBoard() {
	for (int y = FIELD_HEIGHT - 1; y >= BORDER_Y; y--) {
		// Get a row from the field
		uint16_t row = field[y];

		// Draw active piece
		uint16_t tetraminoShape = TETRAMINO_SHAPES[tetraminoType][tetraminoR];
		int tetraminoRowIndex = y - tetraminoY;
		if (tetraminoRowIndex >= 0 && tetraminoRowIndex < TETRAMINO_SIZE) {
			row |= ((tetraminoShape >> (TETRAMINO_SIZE * tetraminoRowIndex)) &
				TETRAMINO_MASK) << tetraminoX;
		}

		for (uint16_t rowMask = 1 << (BOARD_WIDTH + BORDER_X - 1); (rowMask & ~FIELD_MASK_BORDER) != 0; rowMask >>= 1) {
			Serial.print(row & rowMask ? "[]" : "'.");
		}
		Serial.print(' ');
		Serial.println(y);
	}
	Serial.println();
}

void printField() {
	for (int y = FIELD_HEIGHT - 1; y >= 0; y--) {
		// Get a row from the field
		uint16_t row = field[y];

		// Draw active piece
		uint16_t tetraminoShape = TETRAMINO_SHAPES[tetraminoType][tetraminoR];
		int tetraminoRowIndex = y - tetraminoY;
		if (tetraminoRowIndex >= 0 && tetraminoRowIndex < TETRAMINO_SIZE) {
			row |= ((tetraminoShape >> (TETRAMINO_SIZE * tetraminoRowIndex)) &
				TETRAMINO_MASK) << tetraminoX;
		}

		for (uint16_t rowMask = 0x8000; rowMask != 0; rowMask >>= 1) {
			Serial.print(row & rowMask ? "[]" : "'.");
		}
		Serial.print(' ');
		Serial.println(y);
	}
	Serial.println();
}

