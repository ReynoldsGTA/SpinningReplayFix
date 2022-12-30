#include "plugin.h"

using namespace plugin;

void __declspec(naked) ASM_checkBikePacket() {
    _asm {
        test eax, eax
        je carPacket
        mov bx, 9
        cmp ax, bx
        je bikePacket
        mov eax, 0x45BA65
        jmp eax
     carPacket :
        mov eax, 0x45B952
        jmp eax
     bikePacket :
        fld dword ptr[edi + 0x750]
        mov eax, 0x858B40
        fmul dword ptr[eax]
        mov eax, 0x821B40
        call eax
        mov[esi + 0x24], al
        fld dword ptr[edi + 0x760]
        mov eax, 0x859C34
        fmul dword ptr[eax]
        mov eax, 0x821B40
        call eax
        mov[esi + 0x28], al
        fld dword ptr[edi + 0x754]
        mov eax, 0x858B40
        fmul dword ptr[eax]
        mov eax, 0x821B40
        call eax
        mov[esi + 0x25], al
        fld dword ptr[edi + 0x764]
        mov eax, 0x859C34
        fmul dword ptr[eax]
        mov eax, 0x821B40
        call eax
        mov[esi + 0x29], al
        mov eax, 0x45BA65
        jmp eax
     
    }
}

void __declspec(naked) ASM_checkBikeExtraction() {
    _asm {
        cmp eax, ebp
        je CarExtraction
        mov dx, 9
        cmp ax, dx
        je bikeExtraction
        mov eax, 0x45BEF5
        jmp eax
    carExtraction :
        mov eax, 0x45BC85
        jmp eax
    bikeExtraction :
        movsx edx, byte ptr[edi + 0x24]
        mov[esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        mov eax, 0x858B38
        fmul dword ptr[eax]
        fstp dword ptr[esi + 0x750]
        movsx eax, byte ptr[edi + 0x28]
        mov[esp + 0x0C], eax
        fild dword ptr[esp + 0x0C]
        mov edx, 0x859C44
        fmul dword ptr[edx]
        fstp dword ptr[esi + 0x760]
        movsx ecx, byte ptr[edi + 0x25]
        mov[esp + 0x0C], ecx
        fild dword ptr[esp + 0x0C]
        mov eax, 0x858B38
        fmul dword ptr[eax]
        fstp dword ptr[esi + 0x754]
        movsx edx, byte ptr[edi + 0x29]
        mov[esp + 0x0C], edx
        fild dword ptr[esp + 0x0C]
        mov eax, 0x859C44
        fmul dword ptr[eax]
        fstp dword ptr[esi + 0x764]
        mov eax, 0x45BEF5
        jmp eax
    }
}

class SpinningReplay {
public:

    void patchUsingCodeCaves() {
        //Creation part (from replay entity to packet)

        //jmp 006D4A63
        //nop
        //nop
        //nop
        char jump2bikeifPacket[8] = { 0xE9, 0x14, 0x91, 0x27, 0x00, 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45B94A, jump2bikeifPacket, 8);

        //alter CVehicle offsets to 0x750, 0x760, 0x754 and 0x764 in packet section to read from to CBike->wheelRotation and CBike->wheelSuspensionHeight
        patch::SetChar(0x45B954, 0x50);
        patch::SetChar(0x45B955, 0x07);

        patch::SetChar(0x45B968, 0x60);
        patch::SetChar(0x45B969, 0x07);

        patch::SetChar(0x45B97C, 0x54);
        patch::SetChar(0x45B97D, 0x07);

        patch::SetChar(0x45B990, 0x64);
        patch::SetChar(0x45B991, 0x07);

        //jump to special if to check if current vehicle is a bike
        //mov bx,0009  (move 9, TYPE_BIKE, to bx)
        //cmp ax,bx (compare highest bits of eax to bx, aex stores the VEHICLE_TYPE at this point as it is set before jumping)
        //je 0045B952 (jump to bike wheel extraction)
        //jmp 0045BA65 (jump to after bike wheel extraction)
        char bikeifpacket[20] = { 0x66, 0xBB, 0x09, 0x00, 0x66, 0x39, 0xD8, 0x0F, 0x84, 0xE2, 0x6E, 0xD8, 0xFF, 0xE9, 0xF0, 0x6F, 0xD8, 0xFF, 0x90, 0x90 };
        patch::SetRaw(0x6D4A63, bikeifpacket, 20);

        //jump back to after CAutomobile if in packet creation
        //jmp 0045BA65
        //nop
        char jumpback[6] = { 0xE9, 0xBE, 0x00, 0x00, 0x00, 0x90 };
        patch::SetRaw(0x45B9A2, jumpback, 6);


        //Extraction part (from packet to replay entity)
        //jmp 006D4A8E
        //nop
        //nop
        //nop
        char jump2bikeifExtraction[8] = { 0xE9, 0x0C, 0x8E, 0x27, 0x00, 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45BC7D, jump2bikeifExtraction, 8);

        //alter CVehicle offsets to 0x750, 0x760, 0x754 and 0x764 in extraction section to write to to CBike->wheelRotation and CBike->wheelSuspensionHeight
        patch::SetChar(0x45BC99, 0x50);
        patch::SetChar(0x45BC9A, 0x07);

        patch::SetChar(0x45BCB1, 0x60);
        patch::SetChar(0x45BCB2, 0x07);

        patch::SetChar(0x45BCC9, 0x54);
        patch::SetChar(0x45BCCA, 0x07);

        patch::SetChar(0x45BCE1, 0x64);
        patch::SetChar(0x45BCE2, 0x07);

        //jump to special if to check if current vehicle is a bike
        //mov dx,0009  (move 9, TYPE_BIKE, to dx)
        //cmp ax, dx (compare highest bits of eax to bx, aex stores the VEHICLE_TYPE at this point as it is set before jumping)
        //je 0045BC85 (jump to bike wheel section)
        //jmp 0045BEF5 (jump to after bike wheel section)
        //nop
        //nop
        char bikeifextraction[20] = { 0x66, 0xBA, 0x09, 0x00, 0x66, 0x39, 0xD0, 0x0F, 0x84, 0xEA, 0x71, 0xD8, 0xFF, 0xE9, 0x55, 0x74, 0xD8, 0xFF, 0x90, 0x90 };
        patch::SetRaw(0x6D4A8E, bikeifextraction, 20);

        //jump back to after CAutomobile if in extraction
        //jmp 0045BEF5
        //nop
        //nop
        //nop
        char jumpback2[8] = { 0xE9, 0x0B, 0x02, 0x00, 0x00, 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45BCE5, jumpback2, 8);
    }

    SpinningReplay() {
        patch::RedirectJump(0x45B94A, ASM_checkBikePacket);
        patch::RedirectJump(0x45BC7D, ASM_checkBikeExtraction);
    }



} spinningReplay;
