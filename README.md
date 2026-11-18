# CB-CTT – Primera fase

Implementación en C++ de una solución inicial para el problema **Curriculum-Based Course Timetabling (CB-CTT)** mediante una heurística voraz, con evaluación de restricciones duras y blandas.

## Ambiente y requisitos

Desarrollado y ejecutado en **WSL (Ubuntu)**. Se requiere **G++ con soporte para C++17** y **Make**.

Para instalar WSL con Ubuntu en Windows (PowerShell):

```powershell
wsl --install -d Ubuntu
```

Dentro de Ubuntu, instalar las herramientas de compilación:

```bash
sudo apt update
sudo apt install -y build-essential
```

## Compilación y ejecución

Desde la terminal de WSL, entrar a la carpeta `Avances` y compilar:

```bash
cd /ruta/al/proyecto/Avances
make
```

Ejecutar con una instancia de prueba:

```bash
./cbctt_solver Instances/comp01.ctt
```

Se muestra la evaluación por consola y se guardan los archivos `.sol` (asignaciones) y `.txt` (reporte con tiempo, restricciones y penalización) en `solutions/`.

También se puede usar los comandos del `Makefile` para realizar pruebas:

```bash
make test       # Ejecuta la primera instancia de Instances/
make test-all   # Ejecuta todas las instancias y muestra una tabla resumen
```

También se puede evaluar una solución existente:

```bash
./cbctt_solver Instances/comp01.ctt solutions/Fis0506-1.sol
```

Para limpiar los archivos de compilación:

```bash
make clean
```
