#pragma once
#include "lib/vec.h"
#include "drivers/rtc.h"

class Fat32;
class BlockDevice;

struct VfsEntry {
    String name;
    bool dir = false;
    uint32_t size = 0;
    DateTime mtime{};
    DateTime ctime{};
    uint8_t attr = 0;
};

struct Volume {
    char letter;
    Fat32* fs;
    BlockDevice* dev;
    bool ram = false;
    bool read_only = false;
    String label;
    bool temporary = false;   // montada por el instalador
    bool removable = false;   // pendrive o disco USB
};

namespace vfs {
void init();
// Vuelve a detectar los discos y montar las particiones
void remount();
// Monta temporalmente una partición FAT32 en lectura/escritura (S:, T:...). Devuelve la letra o 0.
// El volumen pasa a ser propietario de 'dev'.
char mount_rw(BlockDevice* dev);
void unmount(char letter);
// Discos extraíbles (USB) conectados o retirados con el sistema en marcha
void disk_added(BlockDevice* disk);
void disk_removed(BlockDevice* disk);
uint32_t generation();            // cambia cada vez que aparece o desaparece una unidad
// Expulsa un disco extraíble: escribe lo pendiente y desmonta todas sus unidades.
// 'model' recibe el nombre del dispositivo para el mensaje de "ya puede quitarlo".
bool eject(char letter, String& model);
Vec<Volume*>& volumes();
Volume* volume(char letter);
bool system_on_ram();             // true si C: es un disco en memoria
Vec<BlockDevice*> blank_disks();  // discos sin tabla de particiones
bool format_disk(BlockDevice* disk); // crea MBR + partición FAT32 "BRYSOS"
void ensure_defaults();           // carpetas iniciales en C:
// Carpetas especiales (nombre según el idioma; se respeta la que ya exista)
String docs_dir();
String pictures_dir();
String trash_dir();

// La carpeta del sistema (C:\BrysOS) es de sólo lectura salvo mientras exista un SystemWrite
// (lo usan la configuración y el actualizador). Fuera de él, crear, modificar, borrar o mover
// algo dentro de ella falla con "acceso denegado".
struct SystemWrite {
    SystemWrite();
    ~SystemWrite();
    SystemWrite(const SystemWrite&) = delete;
    SystemWrite& operator=(const SystemWrite&) = delete;
};
bool is_system_path(const char* path);   // C:\BrysOS o algo dentro de ella

bool list(const char* path, Vec<VfsEntry>& out);
bool stat(const char* path, VfsEntry& out);
bool exists(const char* path);
bool is_dir(const char* path);
// Lee un archivo completo. 'data' se reserva con kmalloc (terminado en '\0'); liberar con kfree.
bool read_file(const char* path, uint8_t*& data, uint32_t& size);
bool write_file(const char* path, const void* data, uint32_t size);
bool mkdir(const char* path);
bool remove(const char* path);       // archivo o directorio vacío
bool remove_tree(const char* path);  // recursivo
bool rename(const char* from, const char* to);
bool copy(const char* from, const char* to);   // archivos y carpetas
bool set_attr(const char* path, uint8_t attr);
const char* error();

// Utilidades de rutas ("C:\carpeta\archivo.txt")
String normalize(const char* path, const char* cwd = nullptr);
String parent(const char* path);
String basename(const char* path);
String join(const char* dir, const char* name);
String extension(const char* path);   // en mayúsculas, sin el punto
String unique_name(const char* dir, const char* base, const char* ext);
String format_size(uint64_t bytes);
}
