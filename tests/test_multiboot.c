/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for the multiboot upload (source/link/multiboot.c), with a fake
 * link standing in for the GBA BIOS. Everything sent is decrypted again here
 * with the protocol's formulas, written out separately, and compared with the
 * payload; the key the console derives from the size is pinned to recorded
 * values, so a change to any part of the upload shows up.
 */
#include <string.h>
#include "unity.h"
#include "link/si_link.h"
#include "link/multiboot.h"

//---------------------------------------------------------------------------------
// fake link: a BIOS that becomes ready after a few resets, and records what it gets
//---------------------------------------------------------------------------------
static int resets, status_reads, ready_after;
static u32 recv_queue[4];
static int recv_count;
static u32 sent[0x2000];
static int sent_count;

void si_link_reset(void)
{
	resets++;
}
u8 si_link_status(void)
{
	return ++status_reads > ready_after ? 0x10 : 0;
}
u32 si_link_recv(void)
{
	return recv_count < 4 ? recv_queue[recv_count++] : 0;
}
void si_link_send(u32 msg)
{
	if(sent_count < 0x2000)
	{
		sent[sent_count++] = msg;
	}
}

// payloads of a few sizes from this, the real one is 8 byte aligned like them
static u8 rom[0x4000];

void setUp(void)
{
	resets = status_reads = ready_after = 0;
	recv_count = sent_count = 0;
	u32 i;
	for(i = 0; i < sizeof(rom); i++)
	{
		rom[i] = (u8)(i * 13 + 7);
	}
}

void tearDown(void)
{
}

static u32 bswap32(u32 v)
{
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}
static u32 le32(const u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

// the JOY Bus multiboot checksum: CRC16 with polynomial 0xA1C1 over every
// payload word after the header, least significant bit first, from 0x15A0
static u32 expected_crc(u32 size)
{
	u32 crc = 0x15A0, i;
	int bit;
	for(i = 0xC0; i < size; i += 4)
	{
		u32 w = le32(rom + i);
		for(bit = 0; bit < 32; bit++)
		{
			crc = ((crc ^ w) & 1) ? (crc >> 1) ^ 0xA1C1 : crc >> 1;
			w >>= 1;
		}
	}
	return crc;
}

// sends the first size bytes of the payload with the session key the fake BIOS
// hands out and checks every word that went over the cable; returns the key word sent
static u32 check_upload(u32 size, u32 session_key)
{
	static u32 expected[0x1000], got[0x1000];
	// the BIOS hands out its session key xored with "sedo", byte swapped
	recv_queue[0] = bswap32(session_key) ^ 0x7365646F;
	recv_queue[1] = 0;
	multiboot_send(rom, size);
	// the key word, the header and payload words, then the checksum
	TEST_ASSERT_EQUAL_INT(1 + size / 4 + 1, sent_count);
	TEST_ASSERT_EQUAL_INT(2, recv_count);
	// the header goes over unencrypted, in memory order, and the rest is
	// encrypted with a key stream started from the session key
	u32 key = session_key, i;
	for(i = 0; i < size; i += 4)
	{
		expected[i / 4] = le32(rom + i);
		got[i / 4]      = sent[1 + i / 4];
		if(i >= 0xC0)
		{
			key = key * 0x6177614B + 1;
			got[i / 4] ^= key ^ (0u - (0x02000000 + i)) ^ 0x20796220;
		}
	}
	TEST_ASSERT_EQUAL_HEX32_ARRAY(expected, got, size / 4);
	// then the checksum with the size in the top half, encrypted the same way
	key     = key * 0x6177614B + 1;
	u32 crc = sent[sent_count - 1] ^ key ^ (0u - (0x02000000 + size)) ^ 0x20796220;
	TEST_ASSERT_EQUAL_HEX32(expected_crc(size) | (size << 16), crc);
	return sent[0];
}

// the BIOS isn't ready straight away, the console keeps resetting it until it is
static void test_bios_is_reset_until_ready(void)
{
	ready_after = 3;
	multiboot_wait_bios();
	TEST_ASSERT_EQUAL_INT(4, status_reads);
	TEST_ASSERT_EQUAL_INT(4, resets);
}

// the key word depends on the size only, the values are recorded from this implementation
static void test_upload_small_payload(void)
{
	TEST_ASSERT_EQUAL_HEX32(0xD9CFE1CB, check_upload(0x200, 0x12345678));
}

static void test_upload_large_payload(void)
{
	TEST_ASSERT_EQUAL_HEX32(0xE8DCEAB3, check_upload(0x4000, 0xCAFEF00D));
}

static void test_key_word_is_the_same_for_any_session_key(void)
{
	TEST_ASSERT_EQUAL_HEX32(0xD9CFE1CB, check_upload(0x200, 0x9ABCDEF0));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_bios_is_reset_until_ready);
	RUN_TEST(test_upload_small_payload);
	RUN_TEST(test_upload_large_payload);
	RUN_TEST(test_key_word_is_the_same_for_any_session_key);
	return UNITY_END();
}
