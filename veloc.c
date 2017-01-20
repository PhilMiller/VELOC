#include <stdlib.h>
#include <stdio.h>

#include "veloc.h"
#include "mpi.h"
#include "scr.h"

/** Standard size of buffer and mas node size.                             */
#define VELOC_BUFS 256

typedef struct VELOCT_dataset {         /** Dataset metadata.              */
    int             id;                 /** ID to search/update dataset.   */
    void            *ptr;               /** Pointer to the dataset.        */
    int             count;              /** Number of elements in dataset. */
    VELOCT_type     type;               /** Data type for the dataset.     */
    int             eleSize;            /** Element size for the dataset.  */
    long            size;               /** Total size of the dataset.     */
} VELOCT_dataset;

/** Array of datasets and all their internal information.                  */
static VELOCT_dataset VELOC_Data[VELOC_BUFS];

/** VELOC data type for chars.                                               */
VELOCT_type VELOC_CHAR;
/** VELOC data type for short integers.                                      */
VELOCT_type VELOC_SHRT;
/** VELOC data type for integers.                                            */
VELOCT_type VELOC_INTG;
/** VELOC data type for long integers.                                       */
VELOCT_type VELOC_LONG;
/** VELOC data type for unsigned chars.                                      */
VELOCT_type VELOC_UCHR;
/** VELOC data type for unsigned short integers.                             */
VELOCT_type VELOC_USHT;
/** VELOC data type for unsigned integers.                                   */
VELOCT_type VELOC_UINT;
/** VELOC data type for unsigned long integers.                              */
VELOCT_type VELOC_ULNG;
/** VELOC data type for single floating point.                               */
VELOCT_type VELOC_SFLT;
/** VELOC data type for double floating point.                               */
VELOCT_type VELOC_DBLE;
/** VELOC data type for long doble floating point.                           */
VELOCT_type VELOC_LDBE;

// initialize restart flag to assume we're not restarting
static int g_recovery = 0;

static int g_rank = -1;

// Number of protected variables
static unsigned int g_nbVar = 0;

// Number of data types
static unsigned int g_nbType = 0; 

static unsigned int g_ckptSize = 0;

static int veloc_InitBasicTypes(VELOCT_dataset* VELOC_Data)
{
    // initialize our type count
    g_nbType = 0;

    int i;
    for (i = 0; i < VELOC_BUFS; i++) {
        VELOC_Data[i].id = -1;
    }

    VELOC_Mem_type(&VELOC_CHAR, sizeof(char));
    VELOC_Mem_type(&VELOC_SHRT, sizeof(short));
    VELOC_Mem_type(&VELOC_INTG, sizeof(int));
    VELOC_Mem_type(&VELOC_LONG, sizeof(long));
    VELOC_Mem_type(&VELOC_UCHR, sizeof(unsigned char));
    VELOC_Mem_type(&VELOC_USHT, sizeof(unsigned short));
    VELOC_Mem_type(&VELOC_UINT, sizeof(unsigned int));
    VELOC_Mem_type(&VELOC_ULNG, sizeof(unsigned long));
    VELOC_Mem_type(&VELOC_SFLT, sizeof(float));
    VELOC_Mem_type(&VELOC_DBLE, sizeof(double));
    VELOC_Mem_type(&VELOC_LDBE, sizeof(long double));

    return VELOC_SUCCESS;
}

/**************************
 * Init / Finalize
 *************************/

int VELOC_Init(char* configFile)
{
    // TODO: pass config file to SCR
    SCR_Init();

    veloc_InitBasicTypes(VELOC_Data);

    // get our rank
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);

    // TODO: use SCR_Have_checkpoint or something like that
    // check to see if we're restarting
    // we write a dummy file from rank 0 on each checkpoint and
    // use SCR_Route_file to look for that (ugly hack)
    if (g_rank == 0) {
        // build file name to dummy file
        char fn[VELOC_MAX_NAME];
        char fn_scr[VELOC_MAX_NAME];
        snprintf(fn, VELOC_MAX_NAME, "dummy.veloc");

        // look for the file (this is how SCR tells us whether
        // we're restarting from a checkpoint or starting over)
        int rc = SCR_Route_file(fn, fn_scr);
        if (rc == SCR_SUCCESS) {
            // we've got the dummy file, so we're restarting
            g_recovery = 1;
        }
    }

    // get flag from rank 0
    MPI_Bcast(&g_recovery, 1, MPI_INT, 0, MPI_COMM_WORLD);

    return VELOC_SUCCESS;
}

int VELOC_Finalize()
{
    SCR_Finalize();
    return VELOC_SUCCESS;
}

/**************************
 * Memory registration
 *************************/

int VELOC_Mem_type(VELOCT_type* type, int size)
{
    type->id   = g_nbType;
    type->size = size;
    g_nbType++;
    return VELOC_SUCCESS;
}

int VELOC_Mem_protect(int id, void* ptr, long count, VELOCT_type type)
{
    int i, prevSize, updated = 0;
    float ckptSize;
    for (i = 0; i < VELOC_BUFS; i++) {
        if (id == VELOC_Data[i].id) {
            prevSize = VELOC_Data[i].size;

            VELOC_Data[i].ptr     = ptr;
            VELOC_Data[i].count   = count;
            VELOC_Data[i].type    = type;
            VELOC_Data[i].eleSize = type.size;
            VELOC_Data[i].size    = type.size * count;

            g_ckptSize += (type.size * count) - prevSize;

            updated = 1;
        }
    }

    if (updated) {
        ckptSize = g_ckptSize / (1024.0 * 1024.0);
        printf("Variable ID %d reseted. Current ckpt. size per rank is %.2fMB.\n", id, ckptSize);
    } else {
        if (g_nbVar >= VELOC_BUFS) {
            printf("Too many variables registered.\n");
            MPI_Abort(MPI_COMM_WORLD, -1);
            MPI_Finalize();
            exit(1);
        }

        VELOC_Data[g_nbVar].id      = id;
        VELOC_Data[g_nbVar].ptr     = ptr;
        VELOC_Data[g_nbVar].count   = count;
        VELOC_Data[g_nbVar].type    = type;
        VELOC_Data[g_nbVar].eleSize = type.size;
        VELOC_Data[g_nbVar].size    = type.size * count;

        g_nbVar++;
        g_ckptSize += type.size * count;

        ckptSize = g_ckptSize / (1024.0 * 1024.0);
        printf("Variable ID %d to protect. Current ckpt. size per rank is %.2fMB.\n", id, ckptSize);
    }

    return VELOC_SUCCESS;
}

/**************************
 * File registration
 *************************/

// like SCR_Route_file
int VELOC_Route_file(const char* name, char* veloc_name)
{
    SCR_Route_file(name, veloc_name);
    return VELOC_SUCCESS;
}

/**************************
 * Restart routines
 *************************/

int VELOC_Have_restart(int* flag)
{
    *flag = g_recovery;
    return VELOC_SUCCESS;
}

int VELOC_Start_restart()
{
    // NOP for now
    return VELOC_SUCCESS;
}

// reads protected memory from file
int VELOC_Mem_restart()
{
    // build checkpoint file name
    char fn[VELOC_MAX_NAME];
    snprintf(fn, VELOC_MAX_NAME, "Ckpt-Rank%d.fti", g_rank);

    // get SCR path to checkpoint file
    char fn_scr[SCR_MAX_FILENAME];
    SCR_Route_file(fn, fn_scr);

    // open file for reading
    FILE* fd = fopen(fn_scr, "rb");
    if (fd == NULL) {
        printf("Could not open VELOC Mem checkpoint file %s\n", fn_scr);
        return VELOC_FAILURE;
    }

    // read protected memory
    int i;
    for (i = 0; i < g_nbVar; i++) {
        size_t bytes = fread(VELOC_Data[i].ptr, 1, VELOC_Data[i].size, fd);
        if (ferror(fd)) {
            printf("Could not read VELOC checkpoint file %s\n", fn_scr);
            fclose(fd);
            return VELOC_FAILURE;
        }
    }

    // close the file
    if (fclose(fd) != 0) {
        printf("Could not close VELOC checkpoint file %s", fn_scr);
        return VELOC_FAILURE;
    }

    return VELOC_SUCCESS;
}

int VELOC_Complete_restart()
{
    // turn off recovery flag
    g_recovery = 0;
    return VELOC_SUCCESS;
}

/**************************
 * Checkpoint routines
 *************************/

// flag returns 1 if checkpoint should be taken, 0 otherwise
int VELOC_Need_checkpoint(int* flag)
{
    SCR_Need_checkpoint(flag);
    return VELOC_SUCCESS;
}

int VELOC_Start_checkpoint()
{
    SCR_Start_checkpoint();
    return VELOC_SUCCESS;
}

// writes protected memory to file
int VELOC_Mem_checkpoint()
{
    // build checkpoint file name
    char fn[VELOC_MAX_NAME];
    snprintf(fn, VELOC_MAX_NAME, "Ckpt-Rank%d.fti", g_rank);

    // get SCR path to checkpoint file
    char fn_scr[SCR_MAX_FILENAME];
    SCR_Route_file(fn, fn_scr);

    // open checkpoint file
    FILE* fd = fopen(fn_scr, "wb");
    if (fd == NULL) {
        printf("VELOC checkpoint file could not be opened %s", fn_scr);
        return VELOC_FAILURE;
    }

    // write protected memory
    int i;
    for (i = 0; i < g_nbVar; i++) {
        if (fwrite(VELOC_Data[i].ptr, VELOC_Data[i].eleSize, VELOC_Data[i].count, fd) != VELOC_Data[i].count) {
            printf("Dataset #%d could not be written to %s\n", VELOC_Data[i].id, fn_scr);
            fclose(fd);
            return VELOC_FAILURE;
        }
    }

    // flush data to disk
    if (fflush(fd) != 0) {
        printf("VELOC checkpoint file could not be flushed %s\n", fn_scr);
        fclose(fd);
        return VELOC_FAILURE;
    }

    // close the file
    if (fclose(fd) != 0) {
        printf("VELOC checkpoint file could not be flushed %s\n", fn_scr);
        return VELOC_FAILURE;
    }

    return VELOC_SUCCESS;
}

int VELOC_Complete_checkpoint(int valid)
{
    SCR_Complete_checkpoint(valid);
    return VELOC_SUCCESS;
}

/**************************
 * convenience functions for existing FTI users
 * (can be implemented fully with above functions)
 ************************/

int VELOC_Mem_save()
{
    // write protected memory to file
    VELOC_Start_checkpoint();
    int rc = VELOC_Mem_checkpoint();
    VELOC_Complete_checkpoint((rc == VELOC_SUCCESS));
    return VELOC_SUCCESS;
}

int VELOC_Mem_recover()
{
    // read protected memory from file
    VELOC_Start_restart();
    VELOC_Mem_restart();
    VELOC_Complete_restart();
    return VELOC_SUCCESS;
}

int VELOC_Mem_snapshot()
{
    // check whether this is a restart
    int have_restart;
    VELOC_Have_restart(&have_restart);
    if (have_restart) {
        // If this is a recovery load checkpoint data
        return VELOC_Mem_recover();
    }

    // otherwise checkpoint if it's time
    int flag;
    VELOC_Need_checkpoint(&flag);
    if (flag) {
        // it's time, take a checkpoint
        return VELOC_Mem_checkpoint();
    }

    return VELOC_SUCCESS;
}