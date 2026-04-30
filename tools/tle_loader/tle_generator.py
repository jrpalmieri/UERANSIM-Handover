import argparse
import math
from datetime import datetime, timezone

def calculate_checksum(line):
    """Calculates the standard TLE modulo 10 checksum."""
    checksum = 0
    for char in line[:-1]: 
        if char.isdigit():
            checksum += int(char)
        elif char == '-':
            checksum += 1
    return str(checksum % 10)

def datetime_to_tle_epoch(dt):
    """Converts a datetime object to the TLE Epoch format (YYDDD.DDDDDDDD)."""
    year = dt.year % 100
    start_of_year = datetime(dt.year, 1, 1, tzinfo=timezone.utc)
    delta = dt - start_of_year
    fractional_day = delta.total_seconds() / 86400.0 + 1.0 
    return f"{year:02d}{fractional_day:012.8f}"

def get_gmst(dt):
    """Calculates Greenwich Mean Sidereal Time (GMST) in degrees for a given datetime."""
    if dt.month <= 2:
        y = dt.year - 1
        m = dt.month + 12
    else:
        y = dt.year
        m = dt.month
    
    d = dt.day + (dt.hour + dt.minute / 60.0 + dt.second / 3600.0) / 24.0
    a = math.floor(y / 100)
    b = 2 - a + math.floor(a / 4)
    jd = math.floor(365.25 * (y + 4716)) + math.floor(30.6001 * (m + 1)) + d + b - 1524.5

    t = (jd - 2451545.0) / 36525.0
    gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t**2 - (t**3 / 38710000.0)
    return gmst % 360.0


def generate_planes(target_lat, target_lon, target_dt, outfilename, sat_name_prefix="SYNTHETIC", num_planes=1, plane_spacing=5.0, plane_config=None):

    tle_lines = []
    for plane_num in range(1,num_planes+1):

        tle_lines.extend(generate_synthetic_plane(target_lat, target_lon, target_dt, sat_name_prefix=sat_name_prefix, plane_num=plane_num, plane_spacing=plane_spacing, plane_config=plane_config))


    # --- Write to File ---
    filename = outfilename
    with open(filename, 'w') as f:
        f.write("\n".join(tle_lines) + "\n")

    print(f"\nAll planes generated and saved to '{filename}'.")

def generate_synthetic_plane(target_lat, target_lon, target_dt, sat_name_prefix="SYNTHETIC", plane_num=1, plane_spacing=5.0, plane_config=None):

    # --- Orbital Parameters ---

    if plane_config is None:
        num_sats = 24
        inclination = 53.0
        mean_motion = 15.10000000 # ~550km altitude
        eccentricity = "0001000"  # Near circular
        arg_perigee = 0.0         # Set perigee to equator crossing
    else:
        num_sats = plane_config.get("num_sats", 24)
        inclination = plane_config.get("inclination", 53.0)
        mean_motion = plane_config.get("mean_motion", 15.10000000)
        eccentricity = plane_config.get("eccentricity", "0001000")
        arg_perigee = plane_config.get("arg_perigee", 0.0)

    print(f"\nGenerating synthetic plane {plane_num} with {num_sats} satellites...")
    print(f"  Targeting Zenith Pass over: {target_lat}°, {target_lon}°")
    print(f"  Target Epoch: {target_dt.strftime('%Y-%m-%d %H:%M:%S UTC')}")
    
    # 1. Spherical Trig to find Mean Anomaly (Argument of Latitude)
    lat_rad = math.radians(target_lat)
    inc_rad = math.radians(inclination)
    
    # Quick sanity check: Target latitude cannot exceed orbital inclination
    if abs(target_lat) > inclination:
        print(f"\nERROR: A satellite with {inclination}° inclination can never fly directly over {target_lat}° latitude.")
        return

    u_rad = math.asin(math.sin(lat_rad) / math.sin(inc_rad))
    base_mean_anomaly = math.degrees(u_rad)
    
    # 2. Spherical Trig to find Geographic Longitude of Ascending Node
    delta_lon_rad = math.asin(math.tan(lat_rad) / math.tan(inc_rad))
    delta_lon_deg = math.degrees(delta_lon_rad)
    
    node_geographic_lon = target_lon - delta_lon_deg
    
    # 3. Convert Geographic Node to Inertial RAAN using Sidereal Time
    gmst = get_gmst(target_dt)
    base_raan = (node_geographic_lon + gmst) % 360.0
    plane_raan = (base_raan + (plane_num - 1) * plane_spacing) % 360.0


    print(f"  Calculated GMST: {gmst:.2f}°")
    print(f"  Target Zenith Sat -> RAAN: {plane_raan:.4f}°, Mean Anomaly: {base_mean_anomaly:.4f}°\n")
    
    # --- Generate TLEs ---
    epoch_str = datetime_to_tle_epoch(target_dt)
    spacing = 360.0 / num_sats
    phase_offset = (plane_num - 1) * (spacing / 2.0)
    
    lines = []
    sat_num_base = 90000 + ((plane_num - 1) * num_sats)  # where to start numbering for this plane
    for i in range(num_sats):
        sat_num = sat_num_base + i
        
        # Calculate Mean Anomaly for this specific satellite
        ma = (base_mean_anomaly - (i * spacing) + phase_offset) % 360.0
        
        name = f"{sat_name_prefix}-{plane_num}-{i+1:02d}"
        
        l1 = f"1 {sat_num}U 26000A   {epoch_str}  .00000000  00000-0  00000-0 0  999 "
        l1 = l1[:-1] + calculate_checksum(l1)
        
        l2 = f"2 {sat_num} {inclination:8.4f} {plane_raan:8.4f} {eccentricity} {arg_perigee:8.4f} {ma:8.4f} {mean_motion:11.8f}    0 "
        l2 = l2[:-1] + calculate_checksum(l2)
        
        lines.extend([name, l1, l2])

        
    print(f"  Successfully generated {num_sats} TLEs for plane {plane_num}.")

    return lines

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate synthetic TLEs for a perfectly spaced LEO satellite plane.")
    
    parser.add_argument('--lat', type=float, required=True, 
                        help="Target Latitude in degrees (e.g., 40.0 for North, -40.0 for South)")
    parser.add_argument('--lon', type=float, required=True, 
                        help="Target Longitude in degrees (e.g., -75.0 for West, 75.0 for East)")
    parser.add_argument('--epoch', type=str, required=True, 
                        help="Epoch time in 'YYYY-MM-DD HH:MM:SS' format (UTC assumed)")
    parser.add_argument('--num-sats', type=int, default=24, 
                        help="Number of satellites in the plane (default: 24)")
    parser.add_argument('--num-planes', type=int, default=1,
                        help="Number of planes (default: 1)")
    parser.add_argument('--spacing', type=float, default=5.0,
                        help="Spacing between satellites in degrees (default: 5.0)")
    parser.add_argument('--outfilename', type=str, default="synthetic_plane.txt",
                        help="Output filename for the generated TLEs (default: synthetic_plane.txt)")
    parser.add_argument('--sat-name-prefix', type=str, default="SYNTHETIC",
                        help="Prefix for satellite names in the TLEs (default: SYNTHETIC)")
    args = parser.parse_args()
    
    try:
        # Parse the string into a timezone-aware UTC datetime object
        dt_obj = datetime.strptime(args.epoch, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
        plane_config={
            "num_sats": args.num_sats,
            "inclination": 53.0,
            "mean_motion": 15.10000000,
            "eccentricity": "0001000",
            "arg_perigee": 0.0,
        }
        generate_planes(args.lat, args.lon, dt_obj, outfilename=args.outfilename, sat_name_prefix=args.sat_name_prefix, num_planes=args.num_planes, plane_spacing=args.spacing, plane_config=plane_config)
    
    except ValueError as e:
        print(f"Time format error: {e}")
        print("Please ensure your epoch matches exactly: 'YYYY-MM-DD HH:MM:SS'")