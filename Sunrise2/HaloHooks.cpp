#include "stdafx.h"

#include "HaloHooks.h"
#include <windef.h>
#include "Detour.h"
#include "Utilities.h"


Detour EventUpdateSpamPreventionDetour;
unsigned int event_update_spam_prevention(unsigned int response_flags, DWORD event_level, int category_index, const char* event_text)
{
	unsigned int result = EventUpdateSpamPreventionDetour.GetOriginal<decltype(&event_update_spam_prevention)>()(
		response_flags,
		event_level,
		category_index,
		event_text
	);

	if (result) {
		fprintf(stdout, "%s\n", event_text);
	}

	return result;
}

VOID SetupHalo3EventsHook(DWORD eventGenerateAddress) {
	EventUpdateSpamPreventionDetour = Detour(
		reinterpret_cast<decltype(&event_update_spam_prevention)>(eventGenerateAddress),
		event_update_spam_prevention
	);
	EventUpdateSpamPreventionDetour.Install();
}

Detour EventManagerFinalizeEventDetour;
// This hook is a bit crusty, for some reason just printing the text can cause crashes on occasion,
// it seems the events are rarely non-printable? I'm really not sure. This works tho.
bool c_event_manager__finalize_event(
	void* thisManager,
	int level,
	int category,
	unsigned long response_flags,
	char* event_text, // s_static_text<0x1000>
	int* event_response // this is actually a struct fwiw.
) {
    char event_text_copy[0x1000];

    size_t copylen = 0;
    if (event_text)
    {
        copylen = strnlen(event_text, sizeof(event_text_copy) - 1);
        memcpy(event_text_copy, event_text, copylen);
        event_text_copy[copylen] = '\0';
    }
    else
    {
        static const char nullmsg[] = "<null>";
        memcpy(event_text_copy, nullmsg, sizeof(nullmsg));
        copylen = sizeof(nullmsg) - 1;
    }

    bool result =
        EventManagerFinalizeEventDetour.GetOriginal<decltype(&c_event_manager__finalize_event)>()(
            thisManager, level, category, response_flags, event_text, event_response
    );

    if (*event_response)
    {
        const unsigned char* p = (const unsigned char*)event_text_copy;
        size_t printable = 0;
        for (; printable < copylen; ++printable)
        {
            unsigned char c = p[printable];
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
                break;
        }

        if (printable == copylen)
        {
            fwrite(event_text_copy, 1, copylen, stdout);
            fputc('\n', stdout);
        }
        else
        {
            fprintf(stdout, "<non-printable %zu bytes>\n", copylen);
        }
    }

    return result;
}

VOID SetupHaloReachEventsHook(DWORD functionAddress) {

	EventManagerFinalizeEventDetour = Detour(
		reinterpret_cast<decltype(&c_event_manager__finalize_event)>(functionAddress),
		c_event_manager__finalize_event
	);
	EventManagerFinalizeEventDetour.Install();
}

Detour SecurityRSAComputeAndVerifySignatureDetour;
BOOL security_rsa_compute_and_verify_signature(void* hash, void* signature)
{
    // skip veficiation
    return TRUE;
}

VOID SetupRSAVerificationHook(DWORD functionAddress) {
	SecurityRSAComputeAndVerifySignatureDetour = Detour(
		reinterpret_cast<decltype(&security_rsa_compute_and_verify_signature)>(functionAddress),
		security_rsa_compute_and_verify_signature
	);
	SecurityRSAComputeAndVerifySignatureDetour.Install();
}