#!/usr/bin/python3
# -*- coding: utf-8 -*-
import numpy as nb
import minimalmodbus
import time

CMD_REFUSE = 1
CMD_ACCEPT = 2

reader = minimalmodbus.Instrument('/dev/ttyUSB0', 1)
reader.serial.baudrate = 9600
reader.serial.timeout = 0.2


def GetID(registers):
    value = nb.int64(0)
    for i in range(3):
        value = value * nb.int64(65536)
        value = value + nb.int64(registers[3-i])
    return value

while(True):

    # Read modules first 4 register
    # register0  Status  0=> no card   1=> card
    # register1  first and second byte
    # register2  third and fourth byte
    # register3  fifth byte in lsb

    holdingRegs = reader.read_registers(0, 4, 3)

    if (holdingRegs[0] & 1) == 1:
        # we got a valid keys
        # let's make int64 key id
        # print(holdingRegs)

        cardID = GetID(holdingRegs)

        print('Found CARD ID ' + hex(cardID), end=" ")

        isValid = input('Valid?(Y/N) and press enter ')

        if isValid == 'Y':
            # validate
            reader.write_register(4, CMD_ACCEPT)
        else:
            reader.write_register(4, CMD_REFUSE)
    time.sleep(0.1)
