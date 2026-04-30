import numpy as np
import pandas as pd
from sgp4.api import Satrec, SatrecArray, jday
from sgp4.ext import invjday
from datetime import datetime



def load_tles_from_file(file_path):

    tle_dict = {}
    names, lines1, lines2 = [], [], []
    satrecs = []
    
    print("1. Reading TLE file...")
    # Parse the 3-line format TLE file
    with open(file_path, 'r') as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]
        
    for i in range(0, len(lines), 3):
        name = lines[i]
        l1 = lines[i+1]
        l2 = lines[i+2]
        tle_dict[name] = (l1, l2)

        names.append(name)
        lines1.append(l1)
        lines2.append(l2)
        satrecs.append(Satrec.twoline2rv(l1, l2))

    print(f"   Loaded {len(satrecs)} satellites.")

    return satrecs, tle_dict


def process_tles(satrecs, names):

    print("2. Vectorized SGP4 Propagation...")
    sat_array = SatrecArray(satrecs)
    
    # Calculate the total Julian Date for every satellite's epoch
    total_jds = np.array([sat.jdsatepoch + sat.jdsatepochF for sat in satrecs])
    
    # Find the index of the most recent epoch
    latest_idx = np.argmax(total_jds)
    latest_jd = satrecs[latest_idx].jdsatepoch
    latest_fr = satrecs[latest_idx].jdsatepochF
    
    # Convert back to a human-readable date just for the terminal output
    y, m, d, h, min, s = invjday(latest_jd + latest_fr)
    print(f"   Target Epoch set to: {y}-{m:02d}-{d:02d} {h:02d}:{min:02d}:{s:02.0f} UTC")
    
    # Create arrays for time (same time for all sats)
    jd_array = np.full(len(satrecs), latest_jd)
    fr_array = np.full(len(satrecs), latest_fr)
    
    # Propagate all satellites at once
    # e: error codes, r: position vectors (km), v: velocity vectors (km/s)
    # This returns shapes: e = (N, 1), r = (N, 1, 3), v = (N, 1, 3)
    e_raw, r_raw, v_raw = sat_array.sgp4(jd_array, fr_array)
    
    # Flatten out the time dimension (column 0) so they become 1D/2D arrays again
    e = e_raw[:, 0]
    r = r_raw[:, 0, :]
    v = v_raw[:, 0, :]
    
    print(f"   Propagation complete. {np.sum(e == 0)} valid satellites, {np.sum(e != 0)} failed to propagate.")

    # Filter out satellites that failed to propagate (e.g., decayed)
    valid_mask = (e == 0)
    valid_names = np.array(names)[valid_mask]
    r_valid = r[valid_mask]
    v_valid = v[valid_mask]

    print("3. Calculating Orbital Elements...")
    # Calculate Angular Momentum (h = r x v)
    h = np.cross(r_valid, v_valid)
    h_mag = np.linalg.norm(h, axis=1)
    
    # Calculate Inclination (degrees)
    inc = np.arccos(np.clip(h[:, 2] / h_mag, -1.0, 1.0)) * (180.0 / np.pi)
    
    # Calculate RAAN (degrees)
    raan = np.arctan2(h[:, 0], -h[:, 1]) * (180.0 / np.pi)
    raan = np.mod(raan, 360.0) # Ensure 0-360 range

    print("4. Calculating Phasing (Argument of Latitude)...")
    # Calculate Ascending Node vector (n)
    n = np.column_stack((-h[:, 1], h[:, 0], np.zeros(len(h))))
    n_mag = np.linalg.norm(n, axis=1)
    r_mag = np.linalg.norm(r_valid, axis=1)
    
    # Dot product of n and r
    dot_nr = np.sum(n * r_valid, axis=1)
    
    # Calculate Argument of Latitude
    arg_lat = np.arccos(np.clip(dot_nr / (n_mag * r_mag), -1.0, 1.0)) * (180.0 / np.pi)
    
    # Quadrant check: if r_z < 0, arg_lat = 360 - arg_lat
    arg_lat = np.where(r_valid[:, 2] < 0, 360.0 - arg_lat, arg_lat)

    print("5. Grouping and Sorting Planes...")
    # Load into a pandas DataFrame for easy grouping
    df = pd.DataFrame({
        'SatName': valid_names,
        'Inclination': inc,
        'RAAN': raan,
        'Arg_Lat': arg_lat
    })

    # Grouping Logic: Round inclination to 1 decimal, RAAN to nearest integer
    # This smooths out observational noise in the TLEs
    df['Inc_Rounded'] = df['Inclination'].round(1)
    df['RAAN_Rounded'] = df['RAAN'].round(0)
    
    # Create a unique group ID for each unique (Inclination, RAAN) pair
    df['Plane_ID'] = df.groupby(['Inc_Rounded', 'RAAN_Rounded']).ngroup() + 1
    
    # Sort by Plane_ID, then by their position in the plane (Arg_Lat)
    df = df.sort_values(by=['Plane_ID', 'Arg_Lat']).reset_index(drop=True)
    
    # Assign sequential order within each plane
    df['Sequence_Order'] = df.groupby('Plane_ID').cumcount() + 1
    
    print("6. Cleaning Data for Simulation...")
    
    # Extract Mean Motion directly from the SGP4 objects (no vis-viva math required!)
    # sat.no_kozai is in radians per minute. Convert to revolutions per day (1440 mins in a day).
    all_mean_motions = np.array([sat.no_kozai for sat in satrecs]) * (1440.0 / (2.0 * np.pi))
    
    # Apply our valid_mask so the length matches the satellites that successfully propagated
    valid_mean_motions = all_mean_motions[valid_mask]
    
    # Add to dataframe
    df['Mean_Motion'] = valid_mean_motions

    # 1. Filter out orbit-raising and decaying sats (Keep ~15.0 to 15.25 revs/day)
    df = df[(df['Mean_Motion'] >= 15.0) & (df['Mean_Motion'] <= 15.25)]
    
    # 2. Filter out planes with too few satellites (e.g., less than 15)
    plane_counts = df['Plane_ID'].value_counts()
    valid_planes = plane_counts[plane_counts >= 15].index
    df = df[df['Plane_ID'].isin(valid_planes)]
    
    # 3. Recalculate sequential order since we dropped rows
    df = df.sort_values(by=['Plane_ID', 'Arg_Lat']).reset_index(drop=True)
    df['Sequence_Order'] = df.groupby('Plane_ID').cumcount() + 1
    
    # Update final output
    final_output = df[['SatName', 'Plane_ID', 'Sequence_Order', 'Inclination', 'RAAN', 'Arg_Lat', 'Mean_Motion']]
    
    return final_output

if __name__ == "__main__":
    # Ensure your text file is in the same directory, or provide the full path
    file_name = "./tle_data/starlink_tles_2026_04_27.txt" 

    try:
        satrecs, tle_dict = load_tles_from_file(file_name)
    except FileNotFoundError:
        print(f"Error: Could not find '{file_name}'. Please ensure the file exists.")    

    names = list(tle_dict.keys())
    results = process_tles(satrecs, names)


    # Save to a CSV for simulation ingestion
    output_file = "starlink_planes.csv"
    results.to_csv(output_file, index=False)
    print(f"\nResults saved to '{output_file}'.")

    # print statistics of planes
    plane_counts = results['Plane_ID'].value_counts().sort_index()
    print(f"Total Planes Detected: {len(plane_counts)}")
    print("\nPlane Counts:")
    for plane_id, count in plane_counts.items():
        print(f"   Plane {plane_id}: {count} satellites")
    # Print a preview of the first 15 satellites
    plane_no = results['Plane_ID'].iloc[0]
    print(f"\nPreview of Plane {plane_no}:")
    print(results[results['Plane_ID'] == plane_no].to_string(index=False))
