#!/bin/bash

# Script para concatenar archivos .c y .h en un solo archivo .txt
# con separadores que indican el nombre original de cada archivo

# Nombre del archivo de salida
output_file="gy91.txt"

# Verificar si el archivo de salida ya existe y preguntar si desea sobrescribirlo
if [ -f "$output_file" ]; then
    echo "El archivo $output_file ya existe."
    read -p "¿Deseas sobrescribirlo? (s/n): " respuesta
    if [ "$respuesta" != "s" ] && [ "$respuesta" != "S" ]; then
        echo "Operación cancelada."
        exit 0
    fi
fi

# Limpiar o crear el archivo de salida
> "$output_file"

# Contador de archivos procesados
contador=0

# Procesar archivos .c y .h en el directorio actual
for archivo in *.c *.h; do
    # Verificar si el archivo existe (evita el caso cuando no hay archivos .c o .h)
    if [ -f "$archivo" ]; then
        echo "Procesando: $archivo"
        
        # Agregar separador y nombre del archivo al archivo de salida
        echo "===========" >> "$output_file"
        echo "$archivo" >> "$output_file"
        echo "===========" >> "$output_file"
        
        # Agregar el contenido del archivo
        cat "$archivo" >> "$output_file"
        
        # Agregar una línea en blanco al final para separar archivos
        echo "" >> "$output_file"
        
        ((contador++))
    fi
done

# Verificar si se procesaron archivos
if [ $contador -eq 0 ]; then
    echo "No se encontraron archivos .c o .h en el directorio actual."
    # Eliminar el archivo vacío
    rm "$output_file"
else
    echo "Proceso completado. Se concatenaron $contador archivos en $output_file"
fi