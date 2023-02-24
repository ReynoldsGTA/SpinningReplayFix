#include "plugin.h"
#include "common.h"

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

//patch adding old camera structue
//oldmatrix location CCamera 0xB6F028 + 9BC = 0xB6F9E4
//matrix location CCamera 0xB6F028 + 974 = 0xB6F99C
//        mov eax, [0xB6F99C]
//mov 0xB6F9E4, eax
void __declspec(naked) ASM_copyMatrixToOldProcessMouse() {
    _asm {
        mov         eax, dword ptr[0xB6F028]
        add         eax, 0x974
        push        eax
        mov         ecx, dword ptr[0xB6F028]
        add         ecx, 0x9BC
        mov         eax, 0x59BBC0
        call        eax
        mov         ecx, [esp + 0x44]
        mov         edx, [esp + 0x48]
        mov         eax, 0x45DC6B
        jmp         eax
    }
}


void __declspec(naked) ASM_copyMatrixToOldFixedCamera() {
    _asm {
        mov         eax, dword ptr[0xB6F028]
        add         eax, 0x974
        push        eax
        mov         ecx, dword ptr[0xB6F028]
        add         ecx, 0x9BC
        mov         eax, 0x59BBC0
        call        eax
        mov ecx, [esp + 0x14]
        mov edx, [esp + 0x18]
        mov eax, 0x45D198
        jmp eax
    }
}

void __declspec(naked) ASM_copyMatrixToOldPlaybackThisFrameInterpolation() {
    _asm {
        mov         eax, 0x721D50
        call        eax
        mov         eax, dword ptr[0xB6F028]
        add         eax, 0x974
        push        eax
        mov         ecx, dword ptr[0xB6F028]
        add         ecx, 0x9BC
        mov         eax, 0x59BBC0
        call        eax
        mov eax, 0x45F3A1
        jmp eax
    }
}

void __declspec(naked) ASM_nn() {
    _asm {
        je ee
        and dword ptr[esi + 0x1C], 0xFFFFFFF7
        jmp eax
    ee :
        mov eax, 0x54CCF2
        jmp eax
    }
}

//patch at 0x5689A3
void __declspec(naked) ASM_addProcessCollisionToReplayLoop() {
    _asm {
        mov esi, [edi]
        mov edx, [esi]
        push edi
        mov ecx, esi
        call dword ptr[edx + 0x2C]
        pop edi
        mov esi, [edi] //CPtrNodeDoubleLink.pItem
        mov edx, [esi + 0x1C]
        mov eax, [esi + 0x18]
        mov edi, [edi + 0x4] //CPtrNodeDoubleLink.pNext
        mov ecx, 0x5689A0
        jmp ecx
    }
}
/*
void __declspec(naked) ASM_addProcessCollisionToReplayLoop() {
    _asm {
        mov esi, [edi]
        mov edx, [esi]
        cmp[esi + 0x36], 0x0A
        jnz label1
        push edi
        mov ecx, esi
        call dword ptr[edx + 0x2C]
        pop edi
        label1 :
        mov esi, [edi] //CPtrNodeDoubleLink.pItem
            mov edx, [esi + 0x1C]
            mov eax, [esi + 0x18]
            mov edi, [edi + 0x4] //CPtrNodeDoubleLink.pNext
            mov ecx, 0x5689A0
            jmp ecx
    }
}
*/

DWORD AfterCheck = 0x6BD1E2;
DWORD BeforeCheck = 0x6BD0CE;
void __declspec(naked) ASM_addCheckBikePreRenderReplayMode() {
    _asm {
        push eax
        mov eax, 0xA43088
        cmp byte ptr[eax], 01
        jnz jumptoafter
        pop eax
        mov[esp + 0x7C], ebx
        jmp AfterCheck
    jumptoafter :
        test byte ptr[esi + 0x42B], 01
        pop eax
        jmp BeforeCheck

    }
}

DWORD AfterAdd = 0x5E2C7F;
DWORD OneLocation = 0x858624;
void __declspec(naked) ASM_removeCollisionOffsetReplayMode() {
    _asm {
        mov eax, 0xA43088
        cmp byte ptr[eax], 01
        jnz jumptoafter2
        mov eax, 0x5E2C7F
        jmp eax
    jumptoafter2 :
        mov eax, 0x858624
        fadd dword ptr[eax]
        mov eax, 0x5E2C7F
        jmp eax
    }
}

void __declspec(naked) ASM_removeEntityCollisionCPedSetPosition() {
    _asm {
        mov eax, 0xA43088
        cmp byte ptr[eax], 01
        jnz jumptoafter3
        mov eax, 0x5E2C77
        jmp eax
    jumptoafter3 :
        mov eax, [esi + 0x14]
        fstp dword ptr[eax + 0x38]
        mov eax, 0x5E2C77
        jmp eax
    }
}

class SpinningReplay {
public:

    void SpinningReplay::patchSparks()
    {
        //patch check if replay is MODE_PLAYBACK in CWorld::Process
        //this patch is not needed anymore because we add process collision into the replay loop instead
        //patch::SetChar(0x568742, 0xFF);

        //patch check in CPhysical apply friction which checks for minimal speed
        //fcomp dword ptr [00858B1C] => fcomp dword ptr[00858B14]
        char sparkCondition[6] = { 0xD8, 0x1D, 0x14, 0x8B, 0x85, 0x00 };
        patch::SetRaw(0x5457AA, sparkCondition, 6);

        //patch entity flags
        char aa[70] = { 0x83, 0xE0, 0x00, 0x0D, 0x81, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x89, 0x46, 0x1C, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x0F, 0xB6, 0x4F, 0x2F, 0x0F, 0xB6, 0x57, 0x2F, 0xC1, 0xE1, 0x1C, 0x33, 0xC8, 0x81, 0xE1, 0xFF, 0xFF, 0xFF, 0x7F, 0xC1, 0xE2, 0x1C, 0x33, 0xCA, 0x66, 0x81, 0x7E, 0x22, 0xB0, 0x01, 0x90, 0x90, 0x90 };
        //char aa[70] = { 0x90, 0x90, 0x90, 0x0D, 0x81, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x89, 0x46, 0x1C, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x0F, 0xB6, 0x4F, 0x2F, 0x0F, 0xB6, 0x57, 0x2F, 0xC1, 0xE1, 0x1C, 0x33, 0xC8, 0x81, 0xE1, 0xFF, 0xFF, 0xFF, 0x7F, 0xC1, 0xE2, 0x1C, 0x33, 0xCA, 0x66, 0x81, 0x7E, 0x22, 0xB0, 0x01, 0x90, 0x90, 0x90 };
        //25 CF FF FF FF 83 C8 01
        //char aa[70] = { 0x90, 0x90, 0x90, 0x25, 0xCF, 0xFF, 0xFF, 0xFF, 0x83, 0xC8, 0x01, 0x90, 0x90, 0x89, 0x46, 0x1C, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x0F, 0xB6, 0x4F, 0x2F, 0x0F, 0xB6, 0x57, 0x2F, 0xC1, 0xE1, 0x1C, 0x33, 0xC8, 0x81, 0xE1, 0xFF, 0xFF, 0xFF, 0x7F, 0xC1, 0xE2, 0x1C, 0x33, 0xCA, 0x66, 0x81, 0x7E, 0x22, 0xB0, 0x01, 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45BBF7, aa, 70);

        //patch physical flags
        //char bb[17] = { 0x83, 0xE7, 0x00, 0x81, 0xCF, 0x02, 0x02, 0x00, 0x10, 0x90, 0x90, 0x90, 0x90, 0x90, 0x89, 0x7E, 0x40 };
        char bb[17] = { 0x90, 0x90, 0x90, 0x81, 0xCF, 0x02, 0x02, 0x00, 0x10, 0x90, 0x90, 0x90, 0x90, 0x90, 0x89, 0x7E, 0x40 };
        //char bb[17] = { 0x83, 0xE7, 0x00, 0x81, 0xCF, 0x22, 0x02, 0x00, 0x10, 0x90, 0x90, 0x90, 0x90, 0x90, 0x89, 0x7E, 0x40 };
        //patch::SetRaw(0x45BF11, bb, 17);

        //remove and for entity flags in Replay::PlayThisFrameInterpolation
        char ccc[3] = { 0x90, 0x90, 0x90 };
        patch::SetRaw(0x45F805, ccc, 3);

        //in CBike::preRender() remove suspension height interaction!
        //patch::SetChar(0x6BD0CD, 0); //using this will fuck up suspension in game
        patch::RedirectJump(0x6BD0C7, ASM_addCheckBikePreRenderReplayMode);

        //nop some write to entity flags
        //char cc[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        //patch::SetRaw(0x45BEDE, cc, 6);

        //set has m_bHasContacted to false in processSectorList
        char dd[4] = { 0x83, 0x66, 0x1C, 0xF7 };
        patch::SetRaw(0x54CCEE, dd, 4);

        patch::RedirectJump(0x45F39C, ASM_copyMatrixToOldPlaybackThisFrameInterpolation);
        patch::RedirectJump(0x568995, ASM_addProcessCollisionToReplayLoop);
        patch::RedirectJump(0x5E2C79, ASM_removeCollisionOffsetReplayMode);
        patch::SetChar(0x5E2C7E, 0x90); //nop last part of add at 0x5E2C79
        
        patch::RedirectJump(0x5E2C71, ASM_removeEntityCollisionCPedSetPosition);
        //nop 0x5E2C74 setting of player position in CPed::ProcessEntityCollision
        //patch::SetChar(0x5E2C74, 0x90);
        //patch::SetChar(0x5E2C75, 0x90);
        //patch::SetChar(0x5E2C76, 0x90);
    }

    SpinningReplay() {
        //patch::RedirectJump(0x45B94A, ASM_checkBikePacket);
        //patch::RedirectJump(0x45BC7D, ASM_checkBikeExtraction);

        patchSparks();
    }



} spinningReplay;
