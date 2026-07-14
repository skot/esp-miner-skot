import { TestBed } from '@angular/core/testing';

import { SystemApiService } from './system.service';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';

describe('SystemApiService', () => {
  let service: SystemApiService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient(), provideHttpClientTesting()]
    });
    service = TestBed.inject(SystemApiService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('should PATCH system settings on a device', () => {
    const httpTesting = TestBed.inject(HttpTestingController);

    service.updateSystem('http://miner.local', { frequency: 400 }).subscribe();

    const request = httpTesting.expectOne('http://miner.local/api/system');
    expect(request.request.method).toBe('PATCH');
    expect(request.request.body).toEqual({ frequency: 400 });
    request.flush({});
    httpTesting.verify();
  });

  it('should GET ASIC tuning status from a device', () => {
    const httpTesting = TestBed.inject(HttpTestingController);

    service.getTuningStatus('http://miner.local').subscribe();

    const request = httpTesting.expectOne('http://miner.local/api/system/tuning');
    expect(request.request.method).toBe('GET');
    request.flush({});
    httpTesting.verify();
  });

  it('should POST to start an ASIC tuning scan', () => {
    const httpTesting = TestBed.inject(HttpTestingController);

    service.startTuning('http://miner.local').subscribe();

    const request = httpTesting.expectOne('http://miner.local/api/system/tuning');
    expect(request.request.method).toBe('POST');
    expect(request.request.body).toEqual({});
    request.flush({});
    httpTesting.verify();
  });
});
