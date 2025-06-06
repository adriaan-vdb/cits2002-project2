#include "mysync.h"

// Initialises variables declared in header file
OPTIONS flags;
HASHTABLE *files;
int ndirectories = 0;
char **directories;

HASHTABLE *sync_files; // An a array of lists of files, where each element in the array represents a directory
// The lists of files represent instructions - where each file either has
// 1. New = true; the current directory doesn't contain an older version of this file (a file needs to be created)
// 2. New = false; the current directory contains an older version of this file (the file needs to be updated)

// Provides synopsis of program usage for appropriate use
void usage()
{
    printf("Usage:  ./mysync  [options: -a -n -p -r -v -i pattern -o pattern]  directory1  directory2  [directory3  ...]\n");
}

// Index of the directory of a file; used to organise sync_files[]
int sync_index(FILES file)
{
    // Iterates through list of directories and returns index when list matches the name of the file's directory
    for (int i = 0; i < ndirectories; i++)
    {
        if (strcmp(directories[i], file.directory) == 0)
        {
            return i;
        }
    }
    fprintf(stderr, "Directory '%s' could not be found in list of given directories \n", file.directory);
    exit(EXIT_FAILURE);
}

// Follows the instructions in sync_files[], to sync the directories
void synchronise_directories()
{
    if (sync_files == NULL)
    {
        fprintf(stderr, "No instructions for syncing - sync_files array is empty \n");
        exit(EXIT_FAILURE);
    }

    if (flags.v)
    {
        printf("\n----------- SYNC COMMENCING -----------\n"); // printf("Directory: %d", 1); // How to work with Sub Directories?
    }

    // Synchronises each directory (one at a time), following the instructions provided by sync_files[]
    for (int j = 0; j < ndirectories; j++)
    {
        // Points to the current (initially, the first) instruction/file item of the current directory
        FILELIST *current = sync_files[j];

        // Iterates through the list of instructions and executes each individually
        while (current != NULL)
        {
            if (flags.v)
            {
                printf("\n");
            }

            // Checks if a file is needing to be created or updated, and works accordingly
            if (current->new) // File needs to be created
            {
                // Takes appropriate action for syncing files that are within subdirectories that currently don't exist in current directory
                if (strchr(current->file.pathname, '/') != NULL)
                {
                    // 1. Try to open subdirectories (until they don't exist)
                    DIR *dir;
                    char *tempstring = malloc(strlen(current->file.pathname) * sizeof(char));
                    CHECK_ALLOC(tempstring);
                    tempstring = strdup(current->file.pathname);
                    tempstring[strlen(current->file.pathname) - strlen(current->file.name) - 1] = '\0';
                    // Tempstring split into tokens so subdirectories are sequentially opened
                    // e.g. tries to open Subdirectory1, then Subdirectory1/Subdirectory2, ... until the subdirectory doesn't exist
                    char *token = strtok(tempstring, "/");
                    char *dirname = concat_strings(directories[j], concat_strings("/", token));
                    while (token != NULL && (dir = opendir(dirname)) != NULL)
                    {
                        token = strtok(NULL, "/");
                        if (token != NULL)
                        {
                            dirname = concat_strings(dirname, concat_strings("/", token));
                        }
                    }

                    // 2. Create any remaining subdirectories
                    // Creates new subdirectories until the full "chain of subdirectories" is finished
                    // e.g. creates Subdirectory1/Subdirectory2/Subdirectory3, Subdirectory1/Subdirectory2/Subdirectory3/Subdirectory4, ...
                    while (token != NULL)
                    {
                        mkdir(dirname, 0777);
                        if (flags.v)
                        {
                            printf("MAKING DIRECTORY: %s \n", dirname);
                        }
                        token = strtok(NULL, "/");
                        if (token != NULL)
                        {
                            dirname = concat_strings(dirname, concat_strings("/", token));
                        }
                        mkdir(dirname, 0777);
                    }
                }

                // Constructs the pathname of the source and destination files
                char *source = concat_strings(concat_strings(current->file.directory, "/"), current->file.pathname);
                char *destination = concat_strings(concat_strings(directories[j], "/"), current->file.pathname);

                if (flags.v)
                {
                    printf("-- %s \n - copied from: %s\n", (char *)destination, (char *)source);
                }

                copy_files(source, destination); // Copies the source file into the destination file

                if (flags.p)
                {
                    // Maintains file permissions
                    mode_t new_mode = current->file.permissions;
                    chmod(destination, new_mode);

                    // Maintains modification time
                    struct utimbuf new_times;
                    new_times.modtime = current->file.mtime;
                    utime(destination, &new_times);

                    if (flags.v)
                    {
                        printf("  - REVERTING MOD TIME: %s", ctime(&new_times.modtime));
                        printf("  - REVERTING PERMISSIONS: %o\n", new_mode);
                    }
                }
            }

            else // File needs to be updated (an older version already exists)
            {
                // Constructs the pathname of the source and destination files
                char *source = concat_strings(concat_strings(current->file.directory, "/"), current->file.pathname);
                char *destination = concat_strings(concat_strings(directories[j], "/"), current->file.pathname);

                if (flags.v)
                {
                    printf("-- %s \n - updated from: %s\n", (char *)destination, (char *)source);
                }

                copy_files(source, destination); // Copies the source file into the destination file

                if (flags.p)
                {
                    // Maintains file permissions
                    mode_t new_mode = current->file.permissions;
                    chmod(destination, new_mode);

                    // Maintains modification time
                    struct utimbuf new_times;
                    new_times.modtime = current->file.mtime;
                    utime(destination, &new_times);

                    if (flags.v)
                    {
                        printf("  - REVERTING MOD TIME: %s", ctime(&new_times.modtime));
                        printf("  - REVERTING PERMISSIONS: %o\n", new_mode);
                    }
                }
            }
            current = current->next; // Sets pointer to next instruction
        }
    }
}

// Scans each directory and stores files in a hashtable
void process_directory(char *dirname, OPTIONS *flags, char *rootdirectoryname)
{
    // Attempts to open the directory
    DIR *dir;
    struct dirent *entry;

    dir = opendir(dirname);
    if (dir == NULL)
    {
        fprintf(stderr, "Unable to open directory '%s' : %s\n", dirname, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Iteratively reads each entry (file or subdirectory) within the directory
    while ((entry = readdir(dir)) != NULL)
    {
        struct stat fileStat; // Used to record the info on an entry

        // Stores the path of the entry
        char *path = malloc(sizeof(char) * (strlen(dirname) + 1 + strlen(entry->d_name) + 1));
        CHECK_ALLOC(path);
        sprintf(path, "%s/%s", dirname, entry->d_name);

        // Gets the info/statistics of the entry
        stat(path, &fileStat);
        if (S_ISDIR(fileStat.st_mode)) // Current entry is a sub-directory
        {
            // Skips . and .. subdirectories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                if (flags->v)
                {
                    printf("- Skipping subdirectory: '%s'\n", entry->d_name);
                }
                continue;
            }

            if (flags->v && flags->r)
            {
                printf(" -- recursively scanning subdirectory: '%s'\n", entry->d_name);
            }

            // Calls process_directory on subdirectory (recursively processes file structure) where -r is present
            if (flags->r)
            {
                // Checks if the subdirectory exists directly under the top-level directory
                if (rootdirectoryname == NULL)
                {
                    process_directory(concat_strings(dirname, concat_strings("/", entry->d_name)), flags, dirname);
                }
                else
                {
                    process_directory(concat_strings(dirname, concat_strings("/", entry->d_name)), flags, rootdirectoryname);
                }
            }
        }
        else // Current entry is a file
        {
            // Checks the conditions for which a file should be ignored
            // 1. If -a flag absent and file begins with a "."
            if (!flags->a && entry->d_name[0] == '.')
            {
                if (flags->v)
                {
                    printf("- Skipping (no tag a): '%s' \n", entry->d_name);
                }
                continue;
            }
            // 2. If -i and -o are present and the file matches a -i pattern
            else if (flags->o && flags->i && in_list(entry->d_name, flags->i_patterns))
            {
                if (flags->v)
                {
                    printf("- Skipping (in ignore list): '%s'\n", entry->d_name);
                }
                continue;
            }
            // 3. If -i is present and the file matches a -i pattern
            else if (flags->i && in_list(entry->d_name, flags->i_patterns))
            {
                if (flags->v)
                {
                    printf("- Skipping (in ignore list): '%s'\n", entry->d_name);
                }
                continue;
            }
            // 4. If -o is present, and the file doesn't match a -o pattern
            else if (flags->o && !in_list(entry->d_name, flags->o_patterns))
            {
                if (flags->v)
                {
                    printf("- Skipping (not in only list): '%s'\n", entry->d_name);
                }
                continue;
            }

            FILES newfile;

            // Sets the pathname (relative to the top-level directory) and the directory name of the file
            if (rootdirectoryname == NULL)
            {
                newfile.directory = (char *)dirname;
                newfile.pathname = path + strlen(dirname) + 1;
            }
            else
            {
                newfile.directory = (char *)rootdirectoryname;
                newfile.pathname = path + strlen(rootdirectoryname) + 1;
            }

            // Records the permissions, modification time and name of the file
            newfile.permissions = fileStat.st_mode;
            newfile.mtime = fileStat.st_mtime;
            newfile.name = strdup(entry->d_name);

            hashtable_add(files, newfile); // Adds the information into the hashtable
        }
    }

    closedir(dir);
}

// Processes the hashtable into instructions for syncing
void analyse_files()
{
    if (flags.v)
    {
        printf("\n----------- FILE ANALYSIS COMMENCING -----------\n"); // printf("Directory: %d", 1); // How to work with Sub Directories?
    }

    // Initialises the sync_files array -> which will be used to store the instructions for each directory
    sync_files = calloc(ndirectories, sizeof(FILELIST *));
    for (int i = 0; i < HASHTABLE_SIZE; i++)
    {
        // For each "row" in the hashtable, view the row and account for any hashtable encoding collisions
        if (files[i] != NULL) // Current hashtable row contains files (isn't empty)
        {
            FILELIST *entry = files[i];
            int discovered = 0;  // Counts the number of files that have been discovered in a particular row
            int total_files = 0; // Stores the total number of files in the hashtable row
            // Sets total_files to the total number of files in the hashtable
            FILELIST *temp = entry;
            while (temp != NULL)
            {
                temp = temp->next;
                total_files++;
            }

            // Account for collisions (infinite relative paths, finite rows on hashtable)
            LIST *discovered_paths = list_new(); // Stores relative path names that have been discovered so far

            // Waits until all files (and relative path names) in the hashtable row have been discovered
            while (discovered < total_files) // Analyse each file with the same relative path (e.g. all files with relative path Folder1/File1)
            {

                entry = files[i];
                // Records the relative location of the current file
                char *relative_location = malloc(sizeof(char) * strlen(entry->file.pathname));
                relative_location = entry->file.pathname;
                // Checks if the relative location has been already discovered - if so, move onto the next entry
                while (list_find(discovered_paths, relative_location))
                {
                    entry = entry->next;
                    // Records the relative location of the current file
                    relative_location = malloc(sizeof(char) * strlen(entry->file.pathname));
                    relative_location = entry->file.pathname;
                }

                FILELIST *analysis = NULL; // List used to store the files with the current relative path
                int analysis_index = 0;
                while (entry != NULL) // Store entries with same relative path together
                {
                    // Collects all the files with the same relative path name together
                    if (strcmp(relative_location, entry->file.pathname) == 0)
                    {
                        analysis = realloc(analysis, (analysis_index + 1) * sizeof(FILELIST));
                        CHECK_ALLOC(analysis)
                        analysis[analysis_index] = *entry;
                        analysis_index++;
                        discovered++;
                    }
                    entry = entry->next;
                }
                // Analyse each entry - two cases
                // 1. Only 1 entry (just copy over to every other directory)
                if (analysis_index == 1)
                {
                    // Adds sync instructions to each directory
                    for (int j = 0; j < ndirectories; j++)
                    {
                        if (sync_index(analysis[0].file) == j)
                            continue; // Doesn't add anything instruction to directory containing the up to date file
                        if (flags.v)
                        {
                            printf("FILE to be created: %s in %s\n", (char *)relative_location, directories[j]);
                        }

                        // Adds file to other directories to be synced in
                        FILELIST *new_file = malloc(sizeof(FILELIST));
                        new_file->file = analysis[0].file;
                        new_file->new = true;
                        new_file->next = sync_files[j];
                        sync_files[j] = new_file;
                    }
                }
                // 2. More than 1 entry i.e. find most up to date version and copy to every other directory
                else if (analysis_index != 0)
                {
                    // Find the most up to date version (latest modification time)
                    int latest_index = 0; // Stores the index of the most up to date file version
                    for (int j = 1; j < analysis_index; j++)
                    {
                        if (difftime(analysis[j].file.mtime, analysis[latest_index].file.mtime) > 0)
                        {
                            latest_index = j;
                        }
                    }
                    // Adds sync instructions to each directory
                    for (int j = 0; j < ndirectories; j++)
                    {
                        if (sync_index(analysis[latest_index].file) == j)
                            continue; // Doesn't add anything instruction to directory containing the up to date file
                        // Check if a previous version of the file exists in the current directory
                        bool new = true;
                        for (int x = 0; x < analysis_index; x++)
                        {
                            if (sync_index(analysis[x].file) == j)
                            {
                                new = false;
                            }
                        }
                        if (flags.v && new)
                        {
                            printf("FILE to be created: %s in %s\n", (char *)relative_location, directories[j]);
                        }
                        else if (flags.v)
                        {
                            printf("FILE to be updated: %s in %s\n", (char *)relative_location, directories[j]);
                        }
                        FILELIST *new_file = malloc(sizeof(FILELIST));
                        new_file->file = analysis[latest_index].file;
                        new_file->new = new;
                        new_file->next = sync_files[j];
                        sync_files[j] = new_file;
                    }
                }
                discovered_paths = list_add(discovered_paths, relative_location);
            }
        }
    }
}

// Creates a list of the provided directories (used to help organise sync_files[] instruction array)
void create_directory_list(int optind, char **argv)
{
    // Computes the maximum length of the given directory names
    int max_length = 0; // Used to evaluate the maximum directory name length
    for (int i = 0; i < ndirectories; i++)
    {
        if (strlen(argv[i + optind]) > max_length)
        {
            max_length = strlen(argv[i + optind]);
        }
    }
    // Allocates an appropriately-sized array to store the directory names
    directories = calloc(ndirectories, sizeof(char) * (max_length + 1));
    for (int i = 0; i < ndirectories; i++)
    {
        directories[i] = argv[optind + i];
    }
}

void start_mysync(int optind, int argc, char **argv)
{
    if (flags.v)
    {
        printf("\n----------- SCAN COMMENCING -----------\n");
    }

    // Initialises hashtable
    files = hashtable_new();

    // Processes each directory
    ndirectories = 0;
    for (int i = optind; i < argc; i++)
    {
        if (flags.v)
        {
            printf("\nscanning toplevel: '%s'\n", argv[i]);
        }
        process_directory(argv[i], &flags, NULL);
        ndirectories++;
    }

    create_directory_list(optind, argv);

    // Check and evaluate given directories
    analyse_files();

    if (flags.v && flags.p)
    {
        printf("\nFiles to have original file permissions and modification times\n");
    }

    // Carry out sync instructions
    if (!flags.n)
    {
        synchronise_directories();
    }
}

int main(int argc, char **argv)
{
    int opt;

    // Initialise -i and -o lists
    flags.i_patterns = list_new();
    flags.o_patterns = list_new();

    // Check and evalute command line options
    while ((opt = getopt(argc, argv, OPTLIST)) != -1)
    {
        switch (opt)
        {
        case 'a':
            flags.a = true;
            break;
        case 'i':
            flags.i = true;
            flags.i_patterns = list_add(flags.i_patterns, optarg);
            break;
        case 'n':
            flags.n = true;
            // NOTE: lack of "break" statement, causes -v to automatically be enabled
        case 'v':
            flags.v = true;
            break;
        case 'o':
            flags.o = true;
            flags.o_patterns = list_add(flags.o_patterns, optarg);
            break;
        case 'p':
            flags.p = true;
            break;
        case 'r':
            flags.r = true;
            break;
        default:
            fprintf(stderr, "Invalid option provided \n");
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if (argc - optind + 1 < 3) // If less than 2 directories are named will produce an error
    {
        fprintf(stderr, "Less than 2 directories are named \n");
        usage();
        exit(EXIT_FAILURE);
    }

    start_mysync(optind, argc, argv);

    exit(EXIT_SUCCESS);
}
