#!/usr/bin/env python3
"""
CSV Compression Script
Recursively searches for CSV files in a directory and compresses them into ZIP archives.
Replaces original CSV files with compressed ZIP files after successful compression.
"""

import os
import zipfile
import multiprocessing as mp
from pathlib import Path
from typing import List, Tuple

# Configuration variables - modify these as needed
SEARCH_DIRECTORY = "/root/gtrade/data"  # Directory to search for CSV files
PROCESS_COUNT = 8  # Number of processes to use for compression
VERBOSE = True  # Enable verbose output

def find_csv_files(directory: str) -> List[str]:
    """Recursively find all CSV files in the given directory."""
    csv_files = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.lower().endswith('.csv'):
                csv_files.append(os.path.join(root, file))
    return csv_files

def compress_csv_file(csv_file: str) -> Tuple[str, bool, str]:
    """
    Compress a single CSV file into a ZIP archive and replace the original file.
    
    Args:
        csv_file: Path to the CSV file to compress
    
    Returns:
        Tuple containing (csv_file_path, success, message)
    """
    try:
        # Generate ZIP file name (same location as CSV, replace extension)
        csv_path = Path(csv_file)
        zip_path = csv_path.with_suffix('.zip')
        
        # Compress the CSV file
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zipf:
            zipf.write(csv_file, csv_path.name)
        
        # Verify the ZIP file was created successfully and is not empty
        if not os.path.exists(zip_path) or os.path.getsize(zip_path) == 0:
            return (csv_file, False, "ZIP file creation failed or is empty")
        
        # Verify ZIP file integrity by trying to read it
        try:
            with zipfile.ZipFile(zip_path, 'r') as zipf:
                zipf.testzip()  # Test ZIP file integrity
        except Exception as e:
            os.remove(zip_path)  # Remove corrupted ZIP file
            return (csv_file, False, f"ZIP file integrity check failed: {str(e)}")
        
        # Only remove original CSV file after successful compression and verification
        original_size = os.path.getsize(csv_file)
        compressed_size = os.path.getsize(zip_path)
        compression_ratio = (1 - compressed_size / original_size) * 100
        
        os.remove(csv_file)
        
        return (csv_file, True, f"Compressed to {zip_path} ({compression_ratio:.1f}% reduction)")
        
    except Exception as e:
        # Clean up ZIP file if it exists and there was an error
        zip_path = Path(csv_file).with_suffix('.zip')
        if os.path.exists(zip_path):
            try:
                os.remove(zip_path)
            except:
                pass
        return (csv_file, False, f"Error: {str(e)}")

def main():
    """Main function to orchestrate CSV file compression."""
    print("CSV Compression Script")
    print("=" * 50)
    
    # Validate input directory
    if not os.path.isdir(SEARCH_DIRECTORY):
        print(f"Error: Directory does not exist: {SEARCH_DIRECTORY}")
        return 1
    
    # Find all CSV files
    print(f"Searching for CSV files in: {SEARCH_DIRECTORY}")
    csv_files = find_csv_files(SEARCH_DIRECTORY)
    
    if not csv_files:
        print("No CSV files found.")
        return 0
    
    print(f"Found {len(csv_files)} CSV files")
    
    # Process files with multiprocessing
    print(f"Starting compression with {PROCESS_COUNT} processes...")
    
    success_count = 0
    error_count = 0
    
    with mp.Pool(processes=PROCESS_COUNT) as pool:
        results = pool.map(compress_csv_file, csv_files)
        
        for csv_file, success, message in results:
            if success:
                success_count += 1
                if VERBOSE:
                    print(f"✓ {csv_file}: {message}")
            else:
                error_count += 1
                print(f"✗ {csv_file}: {message}")
    
    # Summary
    print("=" * 50)
    print(f"Compression completed: {success_count} successful, {error_count} errors")
    
    return 0 if error_count == 0 else 1

if __name__ == '__main__':
    exit(main())