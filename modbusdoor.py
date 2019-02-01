#!/usr/bin/python3
# -*- coding: utf-8 -*-
import sys
import select
import numpy as nb
import minimalmodbus
import pymysql

# #### MODBUS FUNCTION
MReader = minimalmodbus.Instrument('/dev/ttyUSB0', 1)
MReader.serial.baudrate = 9600
MReader.serial.timeout = 0.2
MReader.serial.flushInput()

ReaderAddress = 0
ReaderValid = 0
ReaderLock = 0
ReaderLevel = 0
CardLevel = 0
UserID = ""
UserName = ""


def getReaderStatus(ModbusAddress):
    MReader.address = ModbusAddress
    # put a loop for 3 tries
    for loop in range(3):
        try:
            ReturnValues = MReader.read_register(0, 0, 3)
        except:
            # something wrong let’s flush and try again
            MReader.serial.flushInput()
            ReturnValues = None
            continue
        break
    return ReturnValues


def setReaderCommand(ModbusAddress, Command):
    MReader.address = ModbusAddress
    # put a loop for 3 tries
    for loop in range(3):
        try:
            MReader.write_register(4, Command)
        except:
            # something wrong let’s flush and try again
            MReader.serial.flush()
            continue
        break

CMD_REFUSE = 1
CMD_ACCEPT = 2
CMD_DAY_MODE = 4
CMD_NIGHT_MODE = 8
CMD_LAMP_OFF = 16
CMD_LAMP_ON = 32
CMD_DISABLE = 64
CMD_ENABLE = 128
CMD_ACK_BELL = 256
CMD_ACK_DAMPER = 512
CMD_RELOAD = 1024
CMD_RESET = 0xA000

CMD_DISABLE = 64
CMD_ENABLE = 128


# Get Reader Card ID from modbus register[1..3]
def getReaderStatus(ModbusAddress):
    MReader.address = ModbusAddress
    # put a loop for 3 tries
    for loop in range(3):
        try:
            ReturnValues = MReader.read_register(0, 0, 3)
        except:
            # something wrong let s flush and try again
            MReader.serial.flushInput()
            ReturnValues = None
            continue
        break
    return ReturnValues


def GetID(ModbusAddress):
    Registers = MReader.read_registers(5, 6, 3)
    value = ""
    for i in range(5, 0, -1):
        value = value + "{:04X}".format(Registers[i])
    NbChar = Registers[0]*2
    print(value[-NbChar:])
    return value[-NbChar:]


def ValidateCard(sqlcursor, CardID, ReaderLevel):
    global UserID
    global UserName
    global CardLevel
    # let's find the card

    sql_request = 'SELECT card_id,serial_no,user_id,valid,zones_access' + \
                  ' FROM cards  WHERE serial_no = "' + CardID + '"'

    # print("request")
    # print(sql_request)
    # print("-------")

    count = sqlcursor.execute(sql_request)
    # print("After sql! count: {0}".format(count))

    if count == 0:
        return False
    card = sqlcursor.fetchone()

    #  print(card)
    #  print("-----")

    card_id = card[0]
    card_user = card[2]
    card_valid = card[3]
    CardLevel = card[4]

    if card_valid != 1:
        return False

    # ok now let's check who is the owner

    # print("card {0} user is : {1}".format(CardID,card_user))

    sql_request = 'SELECT user_id,zones_access,firstName,lastName ' + \
                  'FROM user_tbl WHERE user_id = ' + str(card_user)
    count = sqlcursor.execute(sql_request)

    print("Count=", count)
    if count == 0:
        return False

    user = sqlcursor.fetchone()
    UserName = user[2] + " " + user[3]
    UserID = user[0]

    if (CardLevel & ReaderLevel) == 0:
        return False
    return True


def CheckReader(sqlcursor, Reader):
    # ok check all info from each reader
    global Reader_id
    global ReaderAddress
    global ReaderValid
    global ReaderLock
    global ReaderLevel
    global UserName

    ReaderAddress = Reader[1]
    ReaderValid = Reader[2]
    # ReaderLock = Reader[2]
    ReaderLevel = Reader[3]

    # print("Reader ",Reader[0], "Modbus : ", ReaderAddress)

    if ReaderAddress < 0:
        # print("skip Reader")
        return

    # read Reader Status
    # Read modules first 4 register
    # register0  Status  0=> no card   1=> card
    # register1  first and second byte
    # register2  third and Fourth byte
    # register3  fifth byte in lsb

    # print("\nReader{0}  Valid: {1}".format(ReaderAddress,ReaderValid),end="")
    sys.stdout.flush()
    ReaderStatus = getReaderStatus(ReaderAddress)
    if ReaderStatus is None:
        # Unable to read reader
        return

    # print(holdingRegs)
    # decode first register
    ReaderGotNewCard = ReaderStatus & 1
    ReaderGotCard = ReaderStatus & 2
    ReaderDamper = ReaderStatus & 4
    ReaderDoorStatus = ReaderStatus & 8
    ReaderBellSwitch = ReaderStatus & 16
    ReaderLight = ReaderStatus & 32
    ReaderDoor = ReaderStatus & 64
    ReaderEnable = ReaderStatus & 128

    # print("S:{0}".format(hex(ReaderStatus)),end="")
    sys.stdout.flush()

    # Is Reader Enable
    if ReaderValid == 0:
        if ReaderEnable == 1:
            # need to disable reader
            setReaderCommand(ReaderAddress, CMD_DISABLE)
        return

    if ReaderEnable == 0:
        # need to disable reader
        setReaderCommand(ReaderAddress, CMD_ENABLE)
        return

    # do we have a new card
    if ReaderGotNewCard == 1:
        # let's check the card in the database

        cardID = GetID(ReaderAddress)
        UserName = "unkown"
        UserID = ""
        # print("Got new Card {0}".format(cardID),end="")
        # sys.stdout.flush()

        valid = ValidateCard(sqlcursor, cardID, ReaderLevel)

        print("Reader:{0},Lv:{1},Card:{2},Lv:{3}".format(
               ReadeAddress, ReaderLevel, cardID, CardLevel), end='')
        print(",user id:{4},name:{5:40s}".format(
              UserID, UserName))

        if valid:
            setReaderCommand(ReaderAddress, CMD_ACCEPT)
            print(",ACCEPT")
        else:
            setReaderCommand(ReaderAddress, CMD_REFUSE)
            print(",REJECTED")

# define Readers Failure Dictionary
FailedReader = {}


# open sql server with user rfid
# user rfid was granted
# rfidreaderdb for table cards,readers and users using the following mysql
# CREATE USER rfiduser
# SET PASSWORD FOR rfiduser = PASSWORD('rfidpassword')
# GRANT SELECT ON rfidreaderdb.cards TO rfiduser
# GRANT SELECT ON rfidreaderdb.readers TO rfiduser
# GRANT SELECT ON rfidreaderdb.users TO rfiduser

sql_con = pymysql.connect(host='localhost', user='rfidreader',
                          passwd='password', db='rfidcardsdb')

cur = sql_con.cursor()

print("RC-522 door system demonstration")
print("D.Perron (c) 2014")
print("Running...")

# let's put the system for night Mode
# print("Set day mode")
# setReaderCommand(0,CMD_DAY_MODE)

while(True):

    # read SQL server and get all readers
    count = cur.execute('SELECT reader_id,modbus_id,enable,' +
                        'zones_access FROM reader_tbl')

    #  print("Check reader count=",count)
    if count == 0:
        continue

    Readers = cur.fetchall()

    for Reader in Readers:
        CheckReader(cur, Reader)

    # quit if enter key pressed
    if select.select([sys.stdin, ], [], [], 0.0)[0]:
        break
