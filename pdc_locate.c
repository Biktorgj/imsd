#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include "glib.h"
#include "mcfg.h"
// Structure to store file path and variable states
typedef struct {
    gchar *path;
    gboolean is_valid;
    guint8 mcc;
    guint8 mnc;
    guint32 carrier_id;
    gchar *internal_name;
} FileInfo;


// Settings
uint8_t debug;
uint8_t fix_carrier_id;
// Input file
size_t INPUT_ELF_OFFSET;
FILE *file_in;
uint8_t *file_in_buff;
uint32_t file_in_sz;
/* We use these to calculate the output buffer size */
uint32_t input_nvitems_size;
uint32_t input_footer_size;

struct Elf32_Ehdr *elf_hdr_in;
struct elf32_phdr *ph0_in;
struct elf32_phdr *ph1_in;
struct elf32_phdr *ph2_in;
struct hash_segment_header *hash_in;
struct mcfg_file_header *mcfg_head_in;
struct mcfg_sub_version_data *mcfg_sub_in;
struct mcfg_footer_section footer_items[MAX_FOOTER_SECTIONS];

/* Do various checks and set pointers to the primary sections of the input file
 */
int check_input_file(guint32 *carrier_id) {
  /*
   * We know MCFG must start at least at 0x2000 bytes without signature, or
   * 0x3000 with it So for now it's enough to check for the minimum size and do
   * more checks later on
   */
  if (file_in_sz < 0x2000) {
    g_printerr( "Error: File is to small!\n");
    return -EINVAL;
  }

  g_print( "Checking ELF header... ");
  elf_hdr_in = (struct Elf32_Ehdr *)file_in_buff;
  if (memcmp(elf_hdr_in->e_ident, ELFMAG, 4) != 0) {
    g_printerr( "Error: ELF header doesn't match!\n");
    return -EINVAL;
  }
  g_print( "OK!\n");
  if (debug) {
    g_print( "ELF Header hex dump:\n");
    int count = 0;
    for (int i = 0; i < sizeof(struct Elf32_Ehdr); i++) {
      g_print( "%.2x ", file_in_buff[i]);
      count++;
      if (count > 15) {
        g_print( "\n");
        count = 0;
      }
    }
    g_print( "\n");
  }
  g_print( "Checking program headers... \n");
  if (elf_hdr_in->e_phnum < 3) {
    g_printerr(
            "Error: Not enough program headers, is this a valid file?\n");
    return -EINVAL;
  }
  /* Program headers */
  g_print( "ELF PH0 Offset: 0x%.4x %i\n", elf_hdr_in->e_phoff,
          elf_hdr_in->e_phoff);
  ph0_in =
      (struct elf32_phdr *)(file_in_buff + (elf_hdr_in->e_phoff +
                                            0 * (sizeof(struct elf32_phdr))));
  g_print( " - ELF data should be at %i bytes (file is %i bytes)\n",
          ph0_in->p_offset, file_in_sz);
  if (file_in_sz < ph0_in->p_offset) {
    g_printerr( "Error: Offset is either bigger than the file or at the "
                    "end of it! (PH0)\n");
    return -EINVAL;
  }
  /* Now we check the program header */
  ph1_in =
      (struct elf32_phdr *)(file_in_buff + (elf_hdr_in->e_phoff +
                                            1 * (sizeof(struct elf32_phdr))));
  g_print(
          " - Hash data data should be at %i bytes (file is %i bytes)\n",
          ph1_in->p_offset, file_in_sz);
  if (file_in_sz < ph1_in->p_offset) {
    g_printerr( "Error: Offset is either bigger than the file or at the "
                    "end of it! (PH1)\n");
    return -EINVAL;
  }

  /* Now we check the program header */
  ph2_in =
      (struct elf32_phdr *)(file_in_buff + (elf_hdr_in->e_phoff +
                                            2 * (sizeof(struct elf32_phdr))));
  g_print( " - MCFG data should be at %i bytes (file is %i bytes)\n",
          ph2_in->p_offset, file_in_sz);
  if (file_in_sz < ph2_in->p_offset) {
    g_printerr( "Error: Offset is either bigger than the file or at the "
                    "end of it! (PH2)\n");
    return -EINVAL;
  }

  hash_in = (struct hash_segment_header *)(file_in_buff + ph1_in->p_offset);
  g_print( " - Checking MCFG header and data... ");
  /* And finally we check if we have the magic string in its expected position
   */
  mcfg_head_in = (struct mcfg_file_header *)(file_in_buff + ph2_in->p_offset);
  if (memcmp(mcfg_head_in->magic, MCFG_FILE_HEADER_MAGIC, 4) != 0) {
    g_printerr( "Error: Invalid  MCFG file MAGIC!\n");
    return -EINVAL;
  }
  mcfg_sub_in =
      (struct mcfg_sub_version_data *)(file_in_buff + ph2_in->p_offset +
                                       sizeof(struct mcfg_file_header));

  g_print( "Found it!\n");
  g_print( "   - Format version: %i\n", mcfg_head_in->format_version);
  g_print( "   - Configuration type: %s\n",
          mcfg_head_in->config_type < 1 ? "HW Config" : "SW Config");
  g_print( "   - Number of items in config: %i\n",
          mcfg_head_in->no_of_items);
  g_print( "   - Carrier ID %i \n", mcfg_head_in->carrier_id);
  g_print( "   - Sub-header data:\n");
  g_print( "     - Magic: %x\n", mcfg_sub_in->magic);
  g_print( "     - Size: %i\n", mcfg_sub_in->len);
  g_print( "     - Data: %.8x\n", mcfg_sub_in->carrier_version);

  if (mcfg_head_in->config_type != MCFG_FILETYPE_SW) {
    g_printerr(
            "Error: Sorry, this program does not support HW filetypes\n");
    return -EINVAL;
  }
  *carrier_id = mcfg_head_in->carrier_id;
  g_print( "File is OK!\n");
  return 0;
}

char *get_section_name(uint8_t section_id) {
  switch (section_id) {
  case MCFG_FOOTER_SECTION_VERSION_1:
    return "Version field";
  case MCFG_FOOTER_SECTION_VERSION_2:
    return "Second version field";
  case MCFG_FOOTER_SECTION_APPLICABLE_MCC_MNC:
    return "Carrier Network code";
  case MCFG_FOOTER_SECTION_PROFILE_NAME:
    return "Carrier Profile name";
  case MCFG_FOOTER_SECTION_ALLOWED_ICCIDS:
    return "Allowed SIM ICC IDs for this profile";
  case MCFG_FOOTER_SECTION_CARRIER_VERSION_ID:
    return "Carrier version ID";
  }

  return "Unknown section";
}

char *get_nvitem_name(uint32_t id) {
  for (int i = 0; i < (sizeof(nvitem_names) / sizeof(nvitem_names[0])); i++) {
    if (id == nvitem_names[i].id) {
      return (char *)nvitem_names[i].name;
    }
  }

  return "Unknwon";
}

int analyze_footer(uint8_t *footer, uint16_t sz, guint8 *mcc, guint8 *mnc, char *internal_name) {
  int sections_parsed = 0;
  int done = 0;
  uint32_t padded_bytes = 0;
  char filename[512];
  memset(footer_items, 0,
         sizeof(struct mcfg_footer_section) * MAX_FOOTER_SECTIONS);
  if (!debug)
    g_print( "\nAnalyzing footer with size of %i bytes\n", sz);
  if (sz <
      (sizeof(struct mcfg_item) + sizeof(struct mcfg_footer_section_version1) +
       sizeof(struct mcfg_footer_section_version2))) {
    g_printerr( "Error: Footer is too short?\n");
    return -EINVAL;
  }
  /* Dump the footer */
  if (debug) {
    g_print( "Footer: hex dump\n");
    int cnt = 0;
    for (int i = 0; i < sz; i++) {
      g_print( "%.2x ", footer[i]);
      cnt++;
      if (cnt > 16) {
        g_print( "\n");
        cnt = 0;
      }
    }
    g_print( "\n");
  }

  struct mcfg_footer *footer_in = (struct mcfg_footer *)footer;
  if (memcmp(footer_in->magic, MCFG_FILE_FOOTER_MAGIC, 8) != 0) {
    g_printerr( "Error: Footer Magic string not found\n");
    return -EINVAL;
  }
  if (footer_in->footer_magic1 != MCFG_ITEM_TYPE_FOOT ||
      footer_in->footer_magic2 != 0xa1) {
    g_printerr( "Error: One of the magic numbers doesn't match\n");
    return -EINVAL;
  }
  g_print(
          "Size:\n - %i bytes\n - Reported: %i bytes\n - Trimmed: %ibytes\n",
          sz, footer_in->len, footer_in->size_trimmed);
  uint32_t *end_marker = (uint32_t *)(footer + sz - 4);
  padded_bytes = *end_marker - footer_in->len;
  g_print( " - Padding at the end: %i bytes \n", padded_bytes);

  uint32_t curr_obj_offset = sizeof(struct mcfg_footer);
  uint32_t max_obj_size = footer_in->len - padded_bytes - sizeof(uint32_t);
  // Pointers to reuse later
  struct mcfg_footer_section_version1 *sec0;
  struct mcfg_footer_section_version2 *sec1;
  struct mcfg_footer_section_2 *sec2;
  struct mcfg_footer_section_carrier_name *sec3;
  struct mcfg_footer_section_allowed_iccids *sec4;
  struct mcfg_footer_section_carrier_id *sec5;
  /* Now find each section */
  g_print( "Footer sections:\n");
  int prev_offset = curr_obj_offset;
  do {
    if (sections_parsed > 15) {
      g_printerr(
              "Error: Exceeded maximum number of sections for the footer\n");
      return -ENOSPC;
    }

    struct mcfg_footer_proto *proto =
        (struct mcfg_footer_proto *)(footer + curr_obj_offset);

    g_print( " - %s (#%i): %i bytes\n", get_section_name(proto->id),
            proto->id, proto->len);

    switch (proto->id) {
    case MCFG_FOOTER_SECTION_VERSION_1: // Fixed size, 2 bytes, CONSTANT
      sec0 = (struct mcfg_footer_section_version1 *)(footer + curr_obj_offset);
      g_print( "   - Version: %i\n", sec0->data);
      break;
    case MCFG_FOOTER_SECTION_VERSION_2: // Fixed size, 4 bytes
      sec1 = (struct mcfg_footer_section_version2 *)(footer + curr_obj_offset);
      if (fix_carrier_id) {
        g_print( "   - Initial version: 0x%.8x", sec1->data);
        while (sec1->data > 0x06000000) {
          sec1->data -= 0x01000000;
        }
        g_print( " --> new: 0x%.8x\n", sec1->data);
      }
      g_print( "   - Initial version: 0x%.8x", sec1->data);
      break;
    case MCFG_FOOTER_SECTION_APPLICABLE_MCC_MNC: // MCC+MNC
      sec2 = (struct mcfg_footer_section_2 *)(footer + curr_obj_offset);
      g_print( "   - MCC-MNC %i-%i\n", sec2->mcc, sec2->mnc);
      *mcc = sec2->mcc;
      *mnc = sec2->mnc;
      break;
    case MCFG_FOOTER_SECTION_PROFILE_NAME: // Carrier name
      sec3 =
          (struct mcfg_footer_section_carrier_name *)(footer + curr_obj_offset);
      g_print( "   - Profile name: %s\n",
              (char *)sec3->carrier_config_name);
    
      memcpy(internal_name, (char *) sec3->carrier_config_name, strlen((char *) sec3->carrier_config_name));
      g_print( ">>>>>>>>>>>>>>>>>>> INTERNAL_NAME: %s\n", internal_name);
      break;

    case MCFG_FOOTER_SECTION_ALLOWED_ICCIDS: // ICCIDs
      sec4 = (struct mcfg_footer_section_allowed_iccids *)(footer +
                                                           curr_obj_offset);
      for (int tmp = 0; tmp < sec4->num_iccids; tmp++) {
        g_print( "   - Allowed ICC ID #%i: %i...\n", tmp,
                sec4->iccids[tmp]);
      }
      break;
    case MCFG_FOOTER_SECTION_CARRIER_VERSION_ID:
      sec5 =
          (struct mcfg_footer_section_carrier_id *)(footer + curr_obj_offset);
      g_print( "   - Carrier version ID: %.4x\n", sec5->carrier_version);
      break;
    default:
      g_print(
          "   - WARNING: %s: Unknown section %i of size %i in the footer at "
          "offset %i\n",
          (char *)sec3->carrier_config_name, proto->id, proto->len,
          curr_obj_offset);
      if (debug) {
        g_print( "Section dump:\n");
        for (int p = 0; p < proto->len; p++) {
          g_print( "%.2x ", proto->data[p]);
        }
        g_print( "\nEnd dump\n");
      }
      break;
    }

    curr_obj_offset += sizeof(struct mcfg_footer_proto) + proto->len;
    if (proto->len == 0) {
      curr_obj_offset++;
    }
    if (curr_obj_offset >= max_obj_size) {
      done = 1;
    }

    footer_items[sections_parsed].id = proto->id;
    footer_items[sections_parsed].size = curr_obj_offset - prev_offset;
    memcpy(footer_items[sections_parsed].blob, (footer + prev_offset),
           curr_obj_offset - prev_offset);
    prev_offset = curr_obj_offset;
    proto = NULL;
    sections_parsed++;
  } while (!done);

  return 0;
}

int process_nv_configuration_data(guint8 *mcc, guint8 *mnc, char *internal_name) {
  g_print("%s: Start 1\n", __func__);
  uint16_t num_items = mcfg_head_in->no_of_items;
  g_print("%s: Start 2, num items: %i\n", __func__, num_items);
  struct item_blob nv_items[num_items];
  g_print("%s: Start 3\n", __func__);
  uint64_t current_offset = ph2_in->p_offset + sizeof(struct mcfg_file_header) +
                            sizeof(struct mcfg_sub_version_data);
  g_print("%s: Start\n", __func__);

  g_print( "Processing items...\n");
  input_nvitems_size = 0;
  for (int i = 0; i < num_items; i++) {
    struct mcfg_item *item =
        (struct mcfg_item *)(file_in_buff + current_offset);
    struct mcfg_nvitem *nvitem;
    struct mcfg_nvfile_part *file_section;

    nv_items[i].offset = current_offset;
    nv_items[i].attrib = item->attrib;
    nv_items[i].type = item->type;
    nv_items[i].id = item->id;
    current_offset += sizeof(struct mcfg_item);
    char efsfilenametmp[256];
    switch (item->type) {
    case MCFG_ITEM_TYPE_NV:
    case MCFG_ITEM_TYPE_UNKNOWN:
      nvitem = (struct mcfg_nvitem *)(file_in_buff + current_offset);
      g_print( " - [NV] Item %i (#%i) @offset %ld: %s \n", i, nvitem->id,
              current_offset, get_nvitem_name(nvitem->id));

      current_offset += sizeof(struct mcfg_nvitem) + nvitem->payload_size;
      nv_items[i].size = current_offset - nv_items[i].offset;
      memcpy(nv_items[i].blob, (file_in_buff + nv_items[i].offset),
             nv_items[i].size);

      nvitem = NULL;
      input_nvitems_size += nv_items[i].size;
      if (debug) {
        int cnt = 0;
        for (int k = 0; k < nv_items[i].size; k++) {
          g_print( "%.2x ", nv_items[i].blob[k]);
          cnt++;
          if (cnt > 32) {
            g_print( "\n");
            cnt = 0;
          }
        }
        g_print( "\n");
      }
      break;
    case MCFG_ITEM_TYPE_NVFILE:
    case MCFG_ITEM_TYPE_FILE:
      memset(efsfilenametmp, 0, 256);

      for (int k = 0; k < 2; k++) {
        file_section =
            (struct mcfg_nvfile_part *)(file_in_buff + current_offset);
        switch (file_section->file_section) {
        case EFS_FILENAME:
          if (debug)
            g_print( " Name: %s (%i bytes)\n",
                    (char *)file_section->payload, file_section->section_len);
          current_offset +=
              sizeof(struct mcfg_nvfile_part) + file_section->section_len;
          memcpy(efsfilenametmp, (char *)file_section->payload,
                 file_section->section_len);
          break;
        case EFS_FILECONTENTS:
          file_section =
              (struct mcfg_nvfile_part *)(file_in_buff + current_offset);
          if (debug)
            g_print( " Data: %i bytes\n", file_section->section_len);
          current_offset +=
              sizeof(struct mcfg_nvfile_part) + file_section->section_len;
          if (efsfilenametmp[0] != 0x00) {
           g_printerr( "EFS File Dump: %s\n", efsfilenametmp);
          } else {
            g_printerr( "EFS File dump: Filename is empty!\n");
          }
          break;
        }
        g_print( " - [File] Item %i (#%i) @offset %ld: %s \n", i,
                item->id, current_offset, efsfilenametmp);
        file_section = NULL;
      }
      nv_items[i].size = current_offset - nv_items[i].offset;
      input_nvitems_size += nv_items[i].size;
   /*   memcpy(nv_items[i].blob, (file_in_buff + nv_items[i].offset),
             nv_items[i].size);*/

      if (debug) {
        int cnt = 0;
        for (int k = 0; k < nv_items[i].size; k++) {
          g_print( "%.2x ", nv_items[i].blob[k]);
          cnt++;
          if (cnt > 32) {
            g_print( "\n");
            cnt = 0;
          }
        }
        g_print( "\nAs str: %s\n", nv_items[i].blob);
      }
      break;
    case MCFG_ITEM_TYPE_FOOT:
      if (debug)
        g_print( "Footer at %ld bytes, size of %ld bytes\n",
                current_offset, file_in_sz - current_offset);
      // REWIND!
      input_footer_size =
          (file_in_sz - (current_offset - sizeof(struct mcfg_item)));
      analyze_footer((file_in_buff + current_offset - sizeof(struct mcfg_item)),
                     input_footer_size, mcc, mnc, internal_name);

      if (i < (num_items - 1))
        g_printerr(
                "WARNING: There's more stuff beyond the footer. Something is "
                "wrong... %i/%i\n",
                i, num_items);

      break;
    default:
      /* We need to break here. There are some types of items who don't follow
       * the same pattern. They sometimes include complete files, secret keys,
       * conditional rules for roaming etc.
       * Instead of holding them in EFS files (or either I'm missing a typedef
       * for some other filetype), they appear as a different item type for some
       * reason
       * Will try to figure it out in the future, but for now, I'll just
       * break here since I don't know how to find the correct offsets for
       * these item types
       */
      g_printerr(
              "Don't know how to handle NV data type %i (0x%.2x) at %ld, "
              "bailing out, "
              "sorry\n",
              item->type, item->type, current_offset);
      return -EINVAL;
      break;
    }

    item = NULL;
  }
  g_print( "%s: end\n", __func__);
  return 0;
}


// Function to check a file's header for specific variables
void check_file_header(const char *input_file, gboolean *is_valid, guint8 *mcc, guint8 *mnc, guint32 *carrier_id, char *internal_name) {
     *is_valid = FALSE;
    *mcc = FALSE;
    *mnc = FALSE;
    *carrier_id = FALSE;
  file_in = fopen(input_file, "rb");
  if (file_in == NULL) {
    g_printerr("Error opening input file %s\n", input_file);
    return;
  }
  fseek(file_in, 0L, SEEK_END);
  file_in_sz = ftell(file_in);
  file_in_buff = malloc(file_in_sz);
  fseek(file_in, 0L, SEEK_SET);
  fread(file_in_buff, file_in_sz, 1, file_in);

  fclose(file_in);
  if (check_input_file(carrier_id) < 0) {
    g_printerr(
            "FATAL: Input file %s is not compatible with this tool :(\n",
            input_file);
    return;
  }

  if (process_nv_configuration_data(mcc, mnc, internal_name) < 0) {
    g_printerr(
            "FATAL: Error processing configuration data from the input file\n");
    return;
  }
    *is_valid = TRUE;
    g_print( "<<<<<<< PROCESSED: %s\n", internal_name);
    free(file_in_buff);
}

// Function to iterate through directories and process files
void loop_through_directory(const char *path, GArray *file_infos) {
    GFile *folder = g_file_new_for_path(path);
    GError *error = NULL;
    GFileEnumerator *enumerator = g_file_enumerate_children(
        folder,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error
    );

    if (!enumerator) {
        g_printerr("Error opening directory %s: %s\n", path, error->message);
        g_clear_error(&error);
        g_object_unref(folder);
        return;
    }

    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(enumerator, NULL, &error)) != NULL) {
        const char *name = g_file_info_get_name(info);
        GFileType type = g_file_info_get_file_type(info);
        GFile *child = g_file_get_child(folder, name);

        if (type == G_FILE_TYPE_DIRECTORY) {
            // Recurse into subdirectories
            loop_through_directory(g_file_get_path(child), file_infos);
        } else if (type == G_FILE_TYPE_REGULAR) {
            if (!g_strcmp0(name, "mcfg_sw.mbn")) {
                gboolean is_valid = FALSE;
                guint8 mcc = 0, mnc = 0;
                guint32 carrier_id = 0;
                const char *file_path = g_file_get_path(child);
                char *internal_name = calloc(128, sizeof(uint8_t));
                // Check the file header for variables
                check_file_header(file_path, &is_valid, &mcc, &mnc, &carrier_id, internal_name);
                g_print(">>>>>>>>>>> DBG %s\n", internal_name);
                // Store the results in the array
                FileInfo file_info = {
                    g_strdup(file_path), // Copy the file path
                    is_valid,
                    mcc,
                    mnc,
                    carrier_id,
                    g_strdup(internal_name)
                };
                g_array_append_val(file_infos, file_info);
                free(internal_name);
            }
        }

        g_object_unref(child);
        g_object_unref(info);
    }

    if (error) {
        g_printerr("Error reading directory '%s': %s\n", path, error->message);
        g_clear_error(&error);
    }

    g_object_unref(enumerator);
    g_object_unref(folder);
}

int scan_pdc_mcfgs(char *directory) {
    // Array to store file information
    GArray *file_infos = g_array_new(FALSE, FALSE, sizeof(FileInfo));
    g_print("[PDCLOCATE] Scanning config directory...\n");
    // Process the directory
    loop_through_directory(directory, file_infos);

    // Print the results
    g_print("Valid \t Carrier ID \t MCC \t MNC \t Internal Name \t Path \n");
    for (guint i = 0; i < file_infos->len; i++) {
        FileInfo *info = &g_array_index(file_infos, FileInfo, i);
        g_print("%s", info->is_valid?"Yes":"No ");
        g_print("\t%.4x", info->carrier_id);
        g_print("\t%u", info->mcc);
        g_print("\t%u", info->mnc);
        g_print("\t%s\n", info->internal_name);
        g_print("\t%s\n", info->path);
    }

    // Free the array
    g_array_free(file_infos, TRUE);
    g_print("[PDCLOCATE] Finish!\n");
    return 0;
}
