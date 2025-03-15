#!/bin/bash
#
# build.sh
#
# ------------------------------------------------------------------------------	
# 
# Builds and copies the firmware files into a firmware directory
# 
# 1. Give it permission to run:
# 
# chmod +x build.sh
# 
# 2. Run it:
# 
# ./build.sh [esphome-filename].yaml
# 
# ------------------------------------------------------------------------------

# Get the YAML file
YAML_FILE="$1"
if [ -z "$YAML_FILE" ]; then
  echo "Please provide the YAML file as argument"
  exit 1
fi

# Extract device name from YAML
DEVICE_NAME=$(grep "name:" "$YAML_FILE" | head -1 | awk '{print $2}' | tr -d "'\"")
echo "Device name: $DEVICE_NAME"

# Compile the firmware
echo "Compiling firmware..."
esphome compile "$YAML_FILE"

# Check if compilation was successful
if [ $? -eq 0 ]; then
  echo "Compilation successful, copying firmware files..."
  
  # Source and destination paths
  SOURCE_DIR=".esphome/build/$DEVICE_NAME/.pioenvs/$DEVICE_NAME"
  DEST_DIR="firmware/$DEVICE_NAME"
  
  # Create destination directory
  mkdir -p "$DEST_DIR"
  
  # Get current timestamp
  TIMESTAMP=$(date +%Y%m%d_%H%M%S)
  
  # Copy firmware files
  for file in "$SOURCE_DIR"/*firmware*.bin; do
    if [ -f "$file" ]; then
      FILENAME=$(basename "$file")
      # Add timestamp to filename
      NEW_FILENAME="${FILENAME%.*}_${TIMESTAMP}.${FILENAME##*.}"
      cp "$file" "$DEST_DIR/$NEW_FILENAME"
      echo "Copied $FILENAME to $DEST_DIR/$NEW_FILENAME"
    fi
  done
  
  echo "Done!"
else
  echo "Compilation failed"
  exit 1
fi