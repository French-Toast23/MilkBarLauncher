[BotW_MPTesting_V208]
moduleMatches = 0x6267BFD0

.origin = codecave

MP_Weather_HLELoc:
.int 0

MP_Weather_HLEName:
.string "WeatherSync"

MP_Weather_ModuleHandle:
.int 0

MP_Weather_ModuleName:
.string "multiplayer"

MP_Weather_WeatherBytesMap:
.byte 0
.byte 1
.byte 2
.byte 3
.byte 4
.byte 5
.byte 6
.byte 7
.byte 8
.byte 0
.byte 0
.byte 0

MP_Spawn_Enabled:
.int 0

MP_Spawn_R3:
.int 0
MP_Spawn_R4:
.int 0
MP_Spawn_R5:
.int 0
MP_Spawn_R6:
.int 0
MP_Spawn_R7:
.int 0
MP_Spawn_R8:
.int 0
MP_Spawn_R9:
.int 0
MP_Spawn_R10:
.int 0

MP_Weather_ChangeWeatherCalled:
b MP_Weather_HookWeather
MP_Weather_ChangeWeather:
lis r6, MP_Weather_WeatherBytesMap@ha			; Load first part of the weather map's address
addi r6, r6, MP_Weather_WeatherBytesMap@l		; Load second part of the address
mulli r26, r26, 0x04				; .byte values are 4 byte aligned, so multiply the offset by 0x04
lbzx r26, r6, r26					; Load the byte that's stored in the weather map using it's address plus an offset which is the original weather value to "map" each one
stb r26, 0x18(r30)					; Original instruction that would store the weather
blr									; Return back to the link register

0x03668FCC = nop					; Replace instruction that checks whether the weather has changed. Since the value is always modified/changed, just disable this branch.
0x03668FEC = bla MP_Weather_ChangeWeatherCalled	; Replace instruction that normally stores the weather with a jump to the weather changing function

MP_Weather_HookWeather:
addi r1, r1, -0x38
stw r3, 0(r1)
stw r4, 4(r1)
stw r5, 8(r1)
stw r6, 12(r1)
stw r7, 16(r1)
stw r8, 20(r1)
stw r9, 24(r1)
stw r10, 28(r1)
stw r11, 32(r1)
stw r12, 36(r1)
mflr r3
stw r3, 40(r1)
mfctr r3
stw r3, 44(r1)

; Check if we already have our dll func addr
lis r11, MP_Weather_HLELoc@ha
lwz r11, MP_Weather_HLELoc@l(r11)
cmpwi r11, 0x0
bne MP_Weather_CallDllFunc
b MP_Weather_RestoreAndExit

MP_Weather_CallDllFunc:
lis r11, MP_Weather_HLELoc@ha
addi r11, r11, MP_Weather_HLELoc@l
cmpwi r11, 0x0
beq MP_Weather_RestoreAndExit
mtctr r11

lis r3, MP_Weather_WeatherBytesMap@ha
addi r3, r3, MP_Weather_WeatherBytesMap@l
bctrl

; Check if actor spawn requested from C++
lis r11, MP_Spawn_Enabled@ha
lwz r11, MP_Spawn_Enabled@l(r11)
cmpwi r11, 0x0
beq MP_Weather_RestoreAndExit

; Clear Enabled flag
li r11, 0x0
lis r12, MP_Spawn_Enabled@ha
stw r11, MP_Spawn_Enabled@l(r12)

; Load registers for createActor_
lis r11, MP_Spawn_R3@ha
lwz r3, MP_Spawn_R3@l(r11)
lis r11, MP_Spawn_R4@ha
lwz r4, MP_Spawn_R4@l(r11)
lis r11, MP_Spawn_R5@ha
lwz r5, MP_Spawn_R5@l(r11)
lis r11, MP_Spawn_R6@ha
lwz r6, MP_Spawn_R6@l(r11)
lis r11, MP_Spawn_R7@ha
lwz r7, MP_Spawn_R7@l(r11)
lis r11, MP_Spawn_R8@ha
lwz r8, MP_Spawn_R8@l(r11)
lis r11, MP_Spawn_R9@ha
lwz r9, MP_Spawn_R9@l(r11)
lis r11, MP_Spawn_R10@ha
lwz r10, MP_Spawn_R10@l(r11)

; Set return address to MP_Weather_RestoreAndExit
lis r11, MP_Weather_RestoreAndExit@ha
addi r11, r11, MP_Weather_RestoreAndExit@l
mtlr r11

; Branch to createActor_
lis r11, 0x037b
ori r11, r11, 0x6040
mtctr r11
bctr

MP_Weather_RestoreAndExit:
lwz r3, 44(r1)
mtctr r3
lwz r3, 40(r1)
mtlr r3
lwz r3, 0(r1)
lwz r4, 4(r1)
lwz r5, 8(r1)
lwz r6, 12(r1)
lwz r7, 16(r1)
lwz r8, 20(r1)
lwz r9, 24(r1)
lwz r10, 28(r1)
lwz r11, 32(r1)
lwz r12, 36(r1)
addi r1, r1, 0x38

b MP_Weather_ChangeWeather
