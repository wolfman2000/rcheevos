#include "rhash.h"

#include "../test_framework.h"
#include "data.h"

#include <stdlib.h>


typedef struct mock_file_data
{
  const char* path;
  const uint8_t* data;
  size_t size;
  size_t pos;
} mock_file_data;

static mock_file_data mock_file_instance[4];

static void* _mock_file_open(const char* path)
{
  int i;
  for (i = 0; i < sizeof(mock_file_instance)/sizeof(mock_file_instance[0]); ++i)
  {
    if (strcmp(path, mock_file_instance[i].path) == 0)
    {
      mock_file_instance[i].pos = 0;
      return &mock_file_instance[i];
    }
  }

  return NULL;
}

static void _mock_file_seek(void* file_handle, size_t offset, int origin)
{
  mock_file_data* file = (mock_file_data*)file_handle;
  switch (origin)
  {
    case SEEK_SET:
      file->pos = offset;
      break;
    case SEEK_CUR:
      file->pos += offset;
      break;
    case SEEK_END:
      file->pos = file->size - offset;
      break;
  }

  if (file->pos > file->size)
    file->pos = file->size;
}

static size_t _mock_file_tell(void* file_handle)
{
  mock_file_data* file = (mock_file_data*)file_handle;
  return file->pos;
}

static size_t _mock_file_read(void* file_handle, void* buffer, size_t count)
{
  mock_file_data* file = (mock_file_data*)file_handle;
  size_t remaining = file->size - file->pos;
  if (count > remaining)
    count = remaining;

  if (count > 0)
  {
    memcpy(buffer, &file->data[file->pos], count);
    file->pos += count;
  }

  return count;
}

static void _mock_file_close(void* file_handle)
{
}

static void* _mock_cd_open_track(const char* path, uint32_t track)
{
  if (track == 1)
  {
    if (strstr(path, ".cue")) 
    {
      mock_file_data* file = (mock_file_data*)_mock_file_open(path);
      if (!file)
        return file;

      return _mock_file_open((const char*)file->data);
    }

    return _mock_file_open(path);
  }

  return NULL;
}

static size_t _mock_cd_read_sector(void* track_handle, uint32_t sector, void* buffer, size_t requested_bytes)
{
  _mock_file_seek(track_handle, sector * 2048, SEEK_SET);
  return _mock_file_read(track_handle, buffer, requested_bytes);
}

static void init_mock_filereader()
{
  int i;

  struct rc_hash_filereader reader;
  reader.open = _mock_file_open;
  reader.seek = _mock_file_seek;
  reader.tell = _mock_file_tell;
  reader.read = _mock_file_read;
  reader.close = _mock_file_close;

  struct rc_hash_cdreader cdreader;
  cdreader.open_track = _mock_cd_open_track;
  cdreader.close_track = _mock_file_close;
  cdreader.read_sector = _mock_cd_read_sector;

  rc_hash_init_custom_filereader(&reader);
  rc_hash_init_custom_cdreader(&cdreader);

  memset(&mock_file_instance, 0, sizeof(mock_file_instance));
  for (i = 0; i < sizeof(mock_file_instance) / sizeof(mock_file_instance[0]); ++i)
    mock_file_instance[i].path = "";
}

static void mock_file(int index, const char* filename, const uint8_t* buffer, size_t buffer_size)
{
  mock_file_instance[index].path = filename;
  mock_file_instance[index].data = buffer;
  mock_file_instance[index].size = buffer_size;
  mock_file_instance[index].pos = 0;
}

static int hash_mock_file(const char* filename, char hash[33], int console_id, const uint8_t* buffer, size_t buffer_size)
{
  mock_file(0, filename, buffer, buffer_size);

  return rc_hash_generate_from_file(hash, console_id, filename);
}

static void iterate_mock_file(struct rc_hash_iterator *iterator, const char* filename, const uint8_t* buffer, size_t buffer_size)
{
  mock_file(0, filename, buffer, buffer_size);

  rc_hash_initialize_iterator(iterator, filename, NULL, 0);
}

/* ========================================================================= */

static void test_hash_full_file(int console_id, const char* filename, size_t size, const char* expected_md5)
{
  uint8_t* image = generate_generic_file(size);
  char hash_buffer[33], hash_file[33], hash_iterator[33];

  /* test full buffer hash */
  int result_buffer = rc_hash_generate_from_buffer(hash_buffer, console_id, image, size);

  /* test full file hash */
  int result_file = hash_mock_file(filename, hash_file, console_id, image, size);

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  iterate_mock_file(&iterator, filename, image, size);
  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_buffer, 1);
  ASSERT_STR_EQUALS(hash_buffer, expected_md5);

  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

static void test_hash_m3u(int console_id, const char* filename, size_t size, const char* expected_md5)
{
  uint8_t* image = generate_generic_file(size);
  char hash_file[33], hash_iterator[33];
  const char* m3u_filename = "test.m3u";

  mock_file(0, filename, image, size);
  mock_file(1, m3u_filename, (uint8_t*)filename, strlen(filename));
  mock_file(1, m3u_filename, (uint8_t*)"# comment\r\ntest.d88", 19);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, console_id, m3u_filename);

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  rc_hash_initialize_iterator(&iterator, m3u_filename, NULL, 0);
  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

/* ========================================================================= */

static void test_hash_3do_bin()
{
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 123456, &image_size);
  char hash_file[33], hash_iterator[33];
  const char* expected_md5 = "9b2266b8f5abed9c12cce780750e88d6";

  mock_file(0, "game.bin", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.bin");

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  mock_file_instance[0].size = 45678901; /* must be > 32MB for iterator to consider CD formats for bin */
  rc_hash_initialize_iterator(&iterator, "game.bin", NULL, 0);
  mock_file_instance[0].size = image_size; /* change it back before doing the hashing */

  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

static void test_hash_3do_cue()
{
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 9347, &image_size);
  char hash_file[33], hash_iterator[33];
  const char* expected_md5 = "257d1d19365a864266b236214dbea29c";

  mock_file(0, "game.bin", image, image_size);
  mock_file(1, "game.cue", (uint8_t*)"game.bin", 8);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.cue");

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  rc_hash_initialize_iterator(&iterator, "game.cue", NULL, 0);
  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

static void test_hash_3do_iso()
{
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 9347, &image_size);
  char hash_file[33], hash_iterator[33];
  const char* expected_md5 = "257d1d19365a864266b236214dbea29c";

  mock_file(0, "game.iso", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.iso");

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  rc_hash_initialize_iterator(&iterator, "game.iso", NULL, 0);
  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

static void test_hash_3do_invalid_header()
{
  /* this is meant to simulate attempting to open a non-3DO CD. TODO: generate PSX CD */
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 12, &image_size);
  char hash_file[33];

  /* make the header not match */
  image[3] = 0x34;

  mock_file(0, "game.bin", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.bin");

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 0);
}

static void test_hash_3do_launchme_case_insensitive()
{
  /* main executable for "Captain Quazar" is "launchme" */
  /* main executable for "Rise of the Robots" is "launchMe" */
  /* main executable for "Road Rash" is "LaunchMe" */
  /* main executable for "Sewer Shark" is "Launchme" */
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 6543, &image_size);
  char hash_file[33];
  const char* expected_md5 = "59622882e3261237e8a1e396825ae4f5";

  memcpy(&image[2048 + 0x14 + 0x48 + 0x20], "launchme", 8);
  mock_file(0, "game.bin", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.bin");

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);
}

static void test_hash_3do_no_launchme()
{
  /* this case should not happen */
  size_t image_size;
  uint8_t* image = generate_3do_bin(1, 6543, &image_size);
  char hash_file[33];

  memcpy(&image[2048 + 0x14 + 0x48 + 0x20], "filename", 8);
  mock_file(0, "game.bin", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.bin");

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 0);
}

static void test_hash_3do_long_directory()
{
  /* root directory for "Dragon's Lair" uses more than one sector */
  size_t image_size;
  uint8_t* image = generate_3do_bin(3, 6543, &image_size);
  char hash_file[33];
  const char* expected_md5 = "8979e876ae502e0f79218f7ff7bd8c2a";

  mock_file(0, "game.bin", image, image_size);

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_3DO, "game.bin");

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);
}

/* ========================================================================= */

static void test_hash_nes_32k()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(32, 0, &image_size);
  char hash[33];
  int result = rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "6a2305a2b6675a97ff792709be1ca857");
  ASSERT_NUM_EQUALS(image_size, 32768);
}

static void test_hash_nes_32k_with_header()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(32, 1, &image_size);
  char hash[33];
  int result = rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  /* NOTE: expectation is that this hash matches the hash in test_hash_nes_32k */
  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "6a2305a2b6675a97ff792709be1ca857");
  ASSERT_NUM_EQUALS(image_size, 32768 + 16);
}

static void test_hash_nes_256k()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(256, 0, &image_size);
  char hash[33];
  int result = rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "545d527301b8ae148153988d6c4fcb84");
  ASSERT_NUM_EQUALS(image_size, 262144);
}

static void test_hash_fds_two_sides()
{
  size_t image_size;
  uint8_t* image = generate_fds_file(2, 0, &image_size);
  char hash[33];
  int result = rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "fd770d4d34c00760fabda6ad294a8f0b");
  ASSERT_NUM_EQUALS(image_size, 65500 * 2);
}

static void test_hash_fds_two_sides_with_header()
{
  size_t image_size;
  uint8_t* image = generate_fds_file(2, 1, &image_size);
  char hash[33];
  int result = rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  /* NOTE: expectation is that this hash matches the hash in test_hash_fds_two_sides */
  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "fd770d4d34c00760fabda6ad294a8f0b");
  ASSERT_NUM_EQUALS(image_size, 65500 * 2 + 16);
}

static void test_hash_nes_file_32k()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(32, 0, &image_size);
  char hash[33];
  int result = hash_mock_file("test.nes", hash, RC_CONSOLE_NINTENDO, image, image_size);
  free(image);

  ASSERT_NUM_EQUALS(result, 1);
  ASSERT_STR_EQUALS(hash, "6a2305a2b6675a97ff792709be1ca857");
  ASSERT_NUM_EQUALS(image_size, 32768);
}

static void test_hash_nes_iterator_32k()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(32, 0, &image_size);
  char hash1[33], hash2[33];
  int result1, result2;
  struct rc_hash_iterator iterator;
  iterate_mock_file(&iterator, "test.nes", image, image_size);
  result1 = rc_hash_iterate(hash1, &iterator);
  result2 = rc_hash_iterate(hash2, &iterator);
  rc_hash_destroy_iterator(&iterator);
  free(image);

  ASSERT_NUM_EQUALS(result1, 1);
  ASSERT_STR_EQUALS(hash1, "6a2305a2b6675a97ff792709be1ca857");

  ASSERT_NUM_EQUALS(result2, 0);
  ASSERT_STR_EQUALS(hash2, "");
}

static void test_hash_nes_file_iterator_32k()
{
  size_t image_size;
  uint8_t* image = generate_nes_file(32, 0, &image_size);
  char hash1[33], hash2[33];
  int result1, result2;
  struct rc_hash_iterator iterator;
  rc_hash_initialize_iterator(&iterator, "test.nes", image, image_size);
  result1 = rc_hash_iterate(hash1, &iterator);
  result2 = rc_hash_iterate(hash2, &iterator);
  rc_hash_destroy_iterator(&iterator);
  free(image);

  ASSERT_NUM_EQUALS(result1, 1);
  ASSERT_STR_EQUALS(hash1, "6a2305a2b6675a97ff792709be1ca857");

  ASSERT_NUM_EQUALS(result2, 0);
  ASSERT_STR_EQUALS(hash2, "");
}

static void test_hash_m3u_with_comments()
{
  const size_t size = 131072;
  uint8_t* image = generate_generic_file(size);
  char hash_file[33], hash_iterator[33];
  const char* m3u_filename = "test.m3u";
  const char* m3u_contents = "#EXTM3U\r\n\r\n#EXTBYT:131072\r\ntest.d88\r\n";
  const char* expected_md5 = "a0f425b23200568132ba76b2405e3933";

  mock_file(0, "test.d88", image, size);
  mock_file(1, m3u_filename, (uint8_t*)m3u_contents, strlen(m3u_contents));

  /* test file hash */
  int result_file = rc_hash_generate_from_file(hash_file, RC_CONSOLE_PC8800, m3u_filename);

  /* test file identification from iterator */
  int result_iterator;
  struct rc_hash_iterator iterator;

  rc_hash_initialize_iterator(&iterator, m3u_filename, NULL, 0);
  result_iterator = rc_hash_iterate(hash_iterator, &iterator);
  rc_hash_destroy_iterator(&iterator);

  /* cleanup */
  free(image);

  /* validation */
  ASSERT_NUM_EQUALS(result_file, 1);
  ASSERT_STR_EQUALS(hash_file, expected_md5);

  ASSERT_NUM_EQUALS(result_iterator, 1);
  ASSERT_STR_EQUALS(hash_iterator, expected_md5);
}

/* ========================================================================= */

void test_hash(void) {
  TEST_SUITE_BEGIN();

  init_mock_filereader();

  /* 3DO */
  TEST(test_hash_3do_bin);
  TEST(test_hash_3do_cue);
  TEST(test_hash_3do_iso);
  TEST(test_hash_3do_invalid_header);
  TEST(test_hash_3do_launchme_case_insensitive);
  TEST(test_hash_3do_no_launchme);
  TEST(test_hash_3do_long_directory);

  /* Apple II */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_APPLE_II, "test.dsk", 143360, "88be638f4d78b4072109e55f13e8a0ac");

  /* Atari 2600 */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_ATARI_2600, "test.bin", 2048, "02c3f2fa186388ba8eede9147fb431c4");

  /* Atari 7800 - includes 128-byte header */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_ATARI_7800, "test.a78", 16384 + 128, "f063cca169b2e49afc339a253a9abadb");

  /* Atari Jaguar */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_ATARI_JAGUAR, "test.jag", 0x400000, "a247ec8a8c42e18fcb80702dfadac14b");

  /* Colecovision */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_COLECOVISION, "test.col", 16384, "455f07d8500f3fabc54906737866167f");

  /* Gameboy */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_GAMEBOY, "test.gb", 131072, "a0f425b23200568132ba76b2405e3933");

  /* Gameboy Color */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_GAMEBOY_COLOR, "test.gbc", 2097152, "cf86acf519625a25a17b1246975e90ae");

  /* Gameboy Advance */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_GAMEBOY_COLOR, "test.gba", 4194304, "a247ec8a8c42e18fcb80702dfadac14b");

  /* Game Gear */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_GAME_GEAR, "test.gg", 524288, "68f0f13b598e0b66461bc578375c3888");

  /* Intellivision */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_INTELLIVISION, "test.bin", 8192, "ce1127f881b40ce6a67ecefba50e2835");

  /* Master System */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_MASTER_SYSTEM, "test.sms", 131072, "a0f425b23200568132ba76b2405e3933");

  /* Mega Drive */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_MEGA_DRIVE, "test.md", 1048576, "da9461b3b0f74becc3ccf6c2a094c516");

  /* Neo Geo Pocket */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_NEOGEO_POCKET, "test.ngc", 2097152, "cf86acf519625a25a17b1246975e90ae");

  /* NES */
  TEST(test_hash_nes_32k);
  TEST(test_hash_nes_32k_with_header);
  TEST(test_hash_nes_256k);
  TEST(test_hash_fds_two_sides);
  TEST(test_hash_fds_two_sides_with_header);

  TEST(test_hash_nes_file_32k);
  TEST(test_hash_nes_file_iterator_32k);
  TEST(test_hash_nes_iterator_32k);

  /* Nintendo 64 */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_NINTENDO_64, "test.n64", 16777216, "d7a0af7f7e89aca1ca75d9c07ce1860f");

  /* Oric (no fixed file size) */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_ORIC, "test.tap", 18119, "953a2baa3232c63286aeae36b2172cef");

  /* PC-8800 */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_PC8800, "test.d88", 348288, "8cca4121bf87200f45e91b905a9f5afd");
  TEST_PARAMS4(test_hash_m3u, RC_CONSOLE_PC8800, "test.d88", 348288, "8cca4121bf87200f45e91b905a9f5afd");

  /* Pokemon Mini */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_POKEMON_MINI, "test.min", 524288, "68f0f13b598e0b66461bc578375c3888");

  /* Sega 32X */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_SEGA_32X, "test.bin", 3145728, "07d733f252896ec41b4fd521fe610e2c");

  /* SG-1000 */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_SG1000, "test.sg", 32768, "6a2305a2b6675a97ff792709be1ca857");

  /* Vectrex */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_SG1000, "test.vec", 4096, "572686c3a073162e4ec6eff86e6f6e3a");

  /* VirtualBoy */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_SG1000, "test.vb", 524288, "68f0f13b598e0b66461bc578375c3888");

  /* WonderSwan */
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_WONDERSWAN, "test.ws", 524288, "68f0f13b598e0b66461bc578375c3888");
  TEST_PARAMS4(test_hash_full_file, RC_CONSOLE_WONDERSWAN, "test.wsc", 4194304, "a247ec8a8c42e18fcb80702dfadac14b");

  /* special cases */
  TEST(test_hash_m3u_with_comments);

  TEST_SUITE_END();
}