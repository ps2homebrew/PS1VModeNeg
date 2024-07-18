#include <malloc.h>
#include <string.h>
#include <stdio.h>

#define NEWLIB_PORT_AWARE
#include <io_common.h>

#include "cnf_lite.h"

// Function prototypes.
static int get_CNF_string(char **CNF_p_p, char **name_p_p, char **value_p_p);

static int file_exists(const char *file)
{
    int fd, result;
    if ((fd = open(file, O_RDONLY)) >= 0) {
        result = 1;
        close(fd);
    } else
        result = 0;

    return result;
}

//________________ From uLaunchELF ______________________
//---------------------------------------------------------------------------
// get_CNF_string is the main CNF parser called for each CNF variable in a
// CNF file. Input and output data is handled via its pointer parameters.
// The return value flags 'false' when no variable is found. (normal at EOF)
//---------------------------------------------------------------------------
static int get_CNF_string(char **CNF_p_p, char **name_p_p, char **value_p_p)
{
    char *np, *vp, *tp = *CNF_p_p;

start_line:
    while ((*tp <= ' ') && (*tp > '\0'))
        tp += 1; // Skip leading whitespace, if any
    if (*tp == '\0')
        return 0;  // but exit at EOF
    np = tp;       // Current pos is potential name
    if (*tp < 'A') // but may be a comment line
    {              // We must skip a comment line
        while ((*tp != '\r') && (*tp != '\n') && (*tp > '\0'))
            tp += 1;     // Seek line end
        goto start_line; // Go back to try next line
    }

    while ((*tp >= 'A') || ((*tp >= '0') && (*tp <= '9')))
        tp += 1; // Seek name end
    if (*tp == '\0')
        return 0; // but exit at EOF

    while ((*tp <= ' ') && (*tp > '\0'))
        *tp++ = '\0'; // zero&skip post-name whitespace
    if (*tp != '=')
        return 0; // exit (syntax error) if '=' missing
    *tp++ = '\0'; // zero '=' (possibly terminating name)

    while ((*tp <= ' ') && (*tp > '\0')      // Skip pre-value whitespace, if any
           && (*tp != '\r') && (*tp != '\n') // but do not pass the end of the line
           && (*tp != '\7')                  // allow ctrl-G (BEL) in value
    )
        tp += 1;
    if (*tp == '\0')
        return 0; // but exit at EOF
    vp = tp;      // Current pos is potential value

    while ((*tp != '\r') && (*tp != '\n') && (*tp != '\0'))
        tp += 1; // Seek line end
    if (*tp != '\0')
        *tp++ = '\0'; // terminate value (passing if not EOF)
    while ((*tp <= ' ') && (*tp > '\0'))
        tp += 1; // Skip following whitespace, if any

    *CNF_p_p   = tp; // return new CNF file position
    *name_p_p  = np; // return found variable name
    *value_p_p = vp; // return found variable value
    return 1;        // return control to caller
} // Ends get_CNF_string

//----------------------------------------------------------------
int Read_SYSTEM_CNF(char *boot_path, char *ver)
{
    // Returns disc type : 0 = failed; 1 = PS1; 2 = PS2;
    size_t CNF_size;
    char *RAM_p, *CNF_p, *name, *value;
    int fd        = -1;
    int Disc_Type = -1; // -1 = Internal : Not Tested;

    // place 3 question mark in ver string
    strcpy(ver, "???");

    fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY);
    if (fd < 0) {
    failed_load:
        if (Disc_Type == -1) {
            // Test PS1 special cases
            if (file_exists("cdrom0:\\PSXMYST\\MYST.CCS;1")) {
                strcpy(boot_path, "SLPS_000.24");
                Disc_Type = 1;
            } else if (file_exists("cdrom0:\\CDROM\\LASTPHOT\\ALL_C.NBN;1")) {
                strcpy(boot_path, "SLPS_000.65");
                Disc_Type = 1;
            } else if (file_exists("cdrom0:\\PSX.EXE;1")) {
                // place 3 question mark in pathname
                strcpy(boot_path, "???");
                Disc_Type = 1;
            }
        }
        if (Disc_Type == -1)
            Disc_Type = 0;
        return Disc_Type;
    } // This point is only reached after succefully opening CNF

    Disc_Type = 0;

    CNF_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    RAM_p = (char *)malloc(CNF_size);
    CNF_p = RAM_p;
    if (CNF_p == NULL) {
        close(fd);
        goto failed_load;
    }
    read(fd, CNF_p, CNF_size); // Read CNF as one long string
    close(fd);
    CNF_p[CNF_size] = '\0'; // Terminate the CNF string

    strcpy(boot_path, "???"); // place 3 question mark in boot path

    while (get_CNF_string(&CNF_p, &name, &value)) {
        // A variable was found, now we dispose of its value.
        if (!strcmp(name, "BOOT2")) { // check for BOOT2 entry
            strcpy(boot_path, value);
            Disc_Type = 2; // If found, PS2 disc type
            break;
        }
        if (!strcmp(name, "BOOT")) { // check for BOOT entry
            strcpy(boot_path, value);
            Disc_Type = 1; // If found, PS1 disc type
            continue;
        }
        if (!strcmp(name, "VER")) { // check for VER entry
            strcpy(ver, value);
            continue;
        }
    } // ends for
    free(RAM_p);
    return Disc_Type;
}
