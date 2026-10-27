#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_CHIPS 4

/* State structure representing a single emulated MCP2515 controller instance */
typedef struct {
  pin_t pin_cs;
  pin_t pin_int;
  pin_t pin_can_h;
  pin_t pin_can_l;
  spi_dev_t spi_dev; 
  uint8_t state;
  uint8_t cmd;
  uint8_t reg;
  uint8_t mask;
  uint8_t regs[256];
  uint8_t spi_buffer[1]; 
} chip_state_t;

/* Registry to map active nodes on the shared virtual CAN bus */
static chip_state_t *g_chips[MAX_CHIPS] = {NULL};
static int g_chip_count = 0;

/* SPI Protocol State Machine States */
#define STATE_IDLE 0
#define STATE_W_REG 1
#define STATE_W_VAL 2
#define STATE_BM_REG 3
#define STATE_BM_MASK 4
#define STATE_BM_VAL 5
#define STATE_R_REG 6
#define STATE_R_VAL 7
#define STATE_R_STATUS 8

/* Generates status flags matching the hardware READ STATUS (0xA0) command */
static uint8_t get_status_byte(chip_state_t *chip) {
  uint8_t status = 0;
  if (chip->regs[0x2C] & 0x01) status |= 0x01; // RX0IF: Message received in RXB0
  if (chip->regs[0x2C] & 0x02) status |= 0x02; // RX1IF: Message received in RXB1
  if (chip->regs[0x30] & 0x08) status |= 0x04; // TXB0REQ: Transmit buffer 0 pending
  if (chip->regs[0x2C] & 0x04) status |= 0x08; // TX0IF: Transmit buffer 0 empty
  if (chip->regs[0x40] & 0x08) status |= 0x10; // TXB1REQ
  if (chip->regs[0x2C] & 0x08) status |= 0x20; // TX1IF
  if (chip->regs[0x50] & 0x08) status |= 0x40; // TXB2REQ
  if (chip->regs[0x2C] & 0x10) status |= 0x80; // TX2IF
  return status;
}

/* Delivers a CAN frame directly into target peer RX buffers and asserts their INT line */
static void transmit_frame(chip_state_t *chip, int buffer_num) {
  uint8_t base_reg = 0x30; 
  if (buffer_num == 1) base_reg = 0x40;
  if (buffer_num == 2) base_reg = 0x50;

  uint32_t id = (chip->regs[base_reg + 1] << 3) | (chip->regs[base_reg + 2] >> 5);
  uint8_t dlc = chip->regs[base_reg + 5] & 0x0F;
  if (dlc > 8) dlc = 8;

  printf("[MCP2515] Sende CAN-Frame (Puffer %d) -> ID: 0x%03X, DLC: %d\n", buffer_num, id, dlc);

  for (int i = 0; i < g_chip_count; i++) {
    chip_state_t *target = g_chips[i];
    if (target != NULL && target != chip) {
      /* Write frame identifiers and DLC to peer's Receive Buffer 0 */
      target->regs[0x61] = (id >> 3) & 0xFF; 
      target->regs[0x62] = (id & 0x07) << 5; 
      target->regs[0x65] = dlc;              
      
      for (int j = 0; j < dlc; j++) {
        target->regs[0x66 + j] = chip->regs[base_reg + 6 + j];
      }
      
      /* Raise RX0IF flag to indicate new incoming data */
      target->regs[0x2C] |= 0x01;
      
      /* Trigger physical low-active interrupt on target microcontroller */
      pin_write(target->pin_int, LOW);
      
      printf("[MCP2515] Frame an Empfaenger ausgeliefert -> Interrupt aktiv!\n");
    }
  }

  chip->regs[base_reg] &= ~0x08; 
}

/* Parses SPI protocol bytes and drives the controller's command state machine */
static void chip_spi_handle_byte(chip_state_t *chip, uint8_t data) {
  switch (chip->state) {
    case STATE_IDLE:
      chip->cmd = data;
      if (data == 0xC0) { // Software Reset Command
        printf("[MCP2515] RESET erhalten.\n");
        chip->regs[0x0F] = 0x80; 
        chip->regs[0x0E] = 0x80; 
      } else if (data == 0x02) { 
        chip->state = STATE_W_REG;
      } else if (data == 0x03) { 
        chip->state = STATE_R_REG;
      } else if (data == 0x05) { 
        chip->state = STATE_BM_REG;
      } else if (data == 0x81) { // RTS Buffer 0
        transmit_frame(chip, 0);
      } else if (data == 0x82) { // RTS Buffer 1
        transmit_frame(chip, 1);
      } else if (data == 0x84) { // RTS Buffer 2
        transmit_frame(chip, 2);
      } else if (data == 0xA0) { // SPI Fast Read Status Command
        chip->state = STATE_R_STATUS;
        chip->spi_buffer[0] = get_status_byte(chip);
      } else if ((data & 0xF0) == 0x90) { // SPI Fast Read RX Buffer Command
        uint8_t n = (data & 0x04) >> 2; // Buffer index: 0 = RXB0, 1 = RXB1
        uint8_t m = (data & 0x02) >> 1; // Start register: 0 = ID-Header, 1 = Data0
        uint8_t base = (n == 0) ? 0x61 : 0x71; 
        if (m == 1) {
          base = (n == 0) ? 0x66 : 0x76; 
        }
        chip->reg = base;
        chip->state = STATE_R_VAL;
        chip->spi_buffer[0] = chip->regs[chip->reg];
      }
      break;

    case STATE_W_REG:
      chip->reg = data;
      chip->state = STATE_W_VAL;
      break;

    case STATE_W_VAL:
      chip->regs[chip->reg] = data;
      
      if (chip->reg == 0x0F) {
        chip->regs[0x0E] = (chip->regs[0x0F] & 0xE0);
      }
      
      if (chip->reg == 0x2C) {
        if (!(chip->regs[0x2C] & 0x01)) {
          pin_write(chip->pin_int, HIGH); // Release external interrupt line when RX0IF is cleared
        }
      }
      
      if (chip->reg == 0x30 && (data & 0x08)) {
        transmit_frame(chip, 0);
      } else if (chip->reg == 0x40 && (data & 0x08)) {
        transmit_frame(chip, 1);
      } else if (chip->reg == 0x50 && (data & 0x08)) {
        transmit_frame(chip, 2);
      }

      chip->reg++; 
      break;

    case STATE_R_REG:
      chip->reg = data;
      chip->state = STATE_R_VAL;
      chip->spi_buffer[0] = chip->regs[chip->reg];
      break;

    case STATE_R_VAL:
      chip->reg++;
      chip->spi_buffer[0] = chip->regs[chip->reg];
      break;

    case STATE_R_STATUS:
      /* Continuously stream updated status while CS remains asserted */
      chip->spi_buffer[0] = get_status_byte(chip);
      break;

    case STATE_BM_REG:
      chip->reg = data;
      chip->state = STATE_BM_MASK;
      break;

    case STATE_BM_MASK:
      chip->mask = data;
      chip->state = STATE_BM_VAL;
      break;

    case STATE_BM_VAL:
      {
        uint8_t old_val = chip->regs[chip->reg];
        chip->regs[chip->reg] = (old_val & ~chip->mask) | (data & chip->mask);
        uint8_t new_val = chip->regs[chip->reg];

        if (chip->reg == 0x0F) {
          chip->regs[0x0E] = (chip->regs[0x0F] & 0xE0);
        }
        
        if (chip->reg == 0x2C) {
          if (!(chip->regs[0x2C] & 0x01)) {
            pin_write(chip->pin_int, HIGH); // Release interrupt line on clearing RX0IF via Bit-Modify
          }
        }
        
        if (chip->reg == 0x30 && (new_val & 0x08) && !(old_val & 0x08)) {
          transmit_frame(chip, 0);
        } else if (chip->reg == 0x40 && (new_val & 0x08) && !(old_val & 0x08)) {
          transmit_frame(chip, 1);
        } else if (chip->reg == 0x50 && (new_val & 0x08) && !(old_val & 0x08)) {
          transmit_frame(chip, 2);
        }

        chip->state = STATE_IDLE;
      }
      break;
  }
}

/* SPI clock-done transfer callback handler */
static void chip_spi_rx(void *user_data, uint8_t *buffer, uint32_t length) {
  chip_state_t *chip = (chip_state_t*)user_data;
  if (pin_read(chip->pin_cs) == HIGH) {
    return;
  }
  chip_spi_handle_byte(chip, buffer[0]);
  spi_start(chip->spi_dev, chip->spi_buffer, 1);
}

/* Handles transitions of the SPI Chip Select (CS) line */
static void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t*)user_data;
  if (pin == chip->pin_cs) {
    if (value == LOW) {
      chip->state = STATE_IDLE;
      chip->spi_buffer[0] = 0;
      spi_start(chip->spi_dev, chip->spi_buffer, 1);
    } else {
      spi_stop(chip->spi_dev);
    }
  }
}

/* Setup routine called by Wokwi to initialize the custom chip's logic resources */
void chip_init() {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));
  
  chip->regs[0x0F] = 0x80; 
  chip->regs[0x0E] = 0x80; 
  chip->state = STATE_IDLE;

  chip->pin_cs = pin_init("CS", INPUT_PULLUP);
  const pin_watch_config_t watch_config = {
    .edge = BOTH,
    .pin_change = chip_pin_change,
    .user_data = chip
  };
  pin_watch(chip->pin_cs, &watch_config);

  chip->pin_int = pin_init("INT", OUTPUT_HIGH);
  chip->pin_can_h = pin_init("CAN_H", INPUT);
  chip->pin_can_l = pin_init("CAN_L", INPUT);

  const spi_config_t spi_config = {
    .sck = pin_init("SCK", INPUT),
    .mosi = pin_init("MOSI", INPUT),
    .miso = pin_init("MISO", OUTPUT),
    .done = chip_spi_rx,
    .user_data = chip
  };
  chip->spi_dev = spi_init(&spi_config);

  if (g_chip_count < MAX_CHIPS) {
    g_chips[g_chip_count++] = chip;
  }
}