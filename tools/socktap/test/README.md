# 🐳 Socktap Docker Container Management Scripts

This repository includes two Bash scripts to automate the launching, updating, stopping, and logging of multiple Docker containers named `socktapX`.

---

## Scripts Included

- [`multiple_container.sh`](#🚀-multiple_containersh)
- [`eliminate_container.sh`](#🛑-eliminate_containersh)

---

## `multiple_container.sh`

This script automates the launching, updating, and management of multiple Docker containers.

### Script Options Summary

| Option           | Default            | Description                                                                 |
|------------------|--------------------|-----------------------------------------------------------------------------|
| `--containers N` | `1`                | Number of Docker containers to launch.                                      |
| `--update`       | `false`            | If present, rebuilds the Docker image using `conteneriza.sh`.              |
| `--terminals`    | `false`            | If present, opens a new terminal tab for each container and shows logs.    |
| `--logs`         | `false`            | If present, waits 5 minutes and then copies the log file from each container. |
| `--logdir PATH`  | `~/Documents`      | Directory where logs will be saved. Created if it doesn’t exist.           |
| `--help`         | `-`                | Prints the help message and exits.                                         |

### Notes:

- Flags like `--update`, `--terminals`, and `--logs` are boolean switches: include them to activate the behavior.
- The `--containers` and `--logdir` options require values.
- If no options are provided, the script runs with 1 container, no logging, and no image rebuild.
- Container names follow the format `socktap1`, `socktap2`, ..., `socktapN`.

---

## `eliminate_container.sh`

This script stops one or more Docker containers with names like `socktapX` and optionally removes them.

### Script Options Summary

| Option           | Default  | Description                                                                 |
|------------------|----------|-----------------------------------------------------------------------------|
| `--containers N` | `1`      | Number of containers to stop, from `socktap1` to `socktapN`.                |
| `--remove`       | `false`  | If present, removes each container after stopping it.                       |
| `--help`         | `-`      | Shows a help message and exits the script.                                  |

### Notes:

- Container names must follow the format `socktap1`, `socktap2`, etc.
- If a container doesn’t exist, the script will print a warning but continue.
- The `--remove` flag is optional and should only be used if you want to permanently delete the containers.

---

